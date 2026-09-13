#include <gtest/gtest.h>
#include <string>
#include <map>
#include <memory>
#include <helpers/ethernet/NetworkPrefs.h>
#include <helpers/ethernet/NetworkPrefsStore.h>
#include <helpers/ethernet/TimeSyncPolicy.h>

class MemoryStream : public Stream {
public:
  std::string data;
  size_t cursor = 0;
  int available() override { return data.size() - cursor; }
  int read() override { return cursor < data.size() ? data[cursor++] : -1; }
  size_t write(uint8_t c) override { data.push_back(c); return 1; }
  size_t print(unsigned char value, int = DEC) override {
    std::string s = std::to_string(value);
    data += s;
    return s.size();
  }
  size_t print(unsigned int value, int = DEC) override {
    std::string s = std::to_string(value); data += s; return s.size();
  }
};

class FakeFS {
public:
  std::map<std::string, std::shared_ptr<MemoryStream>> files;
  bool failWrite = false, failCommit = false;
  struct File : public Stream {
    std::shared_ptr<MemoryStream> stream;
    explicit File(std::shared_ptr<MemoryStream> s = nullptr) : stream(s) {}
    explicit operator bool() const { return bool(stream); }
    int available() override { return stream->available(); }
    int read() override { return stream->read(); }
    size_t write(uint8_t c) override { return stream->write(c); }
    size_t print(unsigned int value, int base = DEC) override { return stream->print(value, base); }
    void close() {}
  };
  bool exists(const char* name) { return files.count(name); }
  File open(const char* name, const char* mode) {
    if (*mode == 'w') {
      if (failWrite) return File();
      files[name] = std::make_shared<MemoryStream>();
    }
    if (!exists(name)) return File();
    files[name]->cursor = 0;
    return File(files[name]);
  }
  bool remove(const char* name) { return files.erase(name) != 0; }
  bool rename(const char* from, const char* to) {
    if ((failCommit && !strcmp(from, "/network.tmp")) || !exists(from) || exists(to)) return false;
    files[to] = files[from]; files.erase(from); return true;
  }
};

TEST(NetworkStore, RepeatedSaveReplacesSettingsOnSpiffsLikeFilesystem) {
  FakeFS fs;
  NetworkPrefs p;
  ASSERT_TRUE(saveNetworkPrefs(fs, p));
  ASSERT_TRUE(p.set("ntp.server", "192.168.1.1"));
  ASSERT_TRUE(saveNetworkPrefs(fs, p));
  NetworkPrefs loaded;
  ASSERT_TRUE(loadNetworkPrefs(fs, loaded));
  EXPECT_STREQ("192.168.1.1", loaded.server);
}

TEST(NetworkStore, FailedWriteAndRenamePreservePreviousSettings) {
  FakeFS fs;
  NetworkPrefs p;
  ASSERT_TRUE(saveNetworkPrefs(fs, p));
  ASSERT_TRUE(p.set("ntp.server", "192.168.1.1"));
  fs.failWrite = true;
  EXPECT_FALSE(saveNetworkPrefs(fs, p));
  fs.failWrite = false;
  fs.failCommit = true;
  EXPECT_FALSE(saveNetworkPrefs(fs, p));
  NetworkPrefs loaded;
  ASSERT_TRUE(loadNetworkPrefs(fs, loaded));
  EXPECT_TRUE(loaded.automatic());
}

TEST(NetworkStore, InterruptedCommitRecoversBackupAndIgnoresTemp) {
  FakeFS fs;
  NetworkPrefs p;
  ASSERT_TRUE(p.set("ntp.server", "192.168.1.1"));
  ASSERT_TRUE(saveNetworkPrefs(fs, p));
  ASSERT_TRUE(fs.rename("/network.json", "/network.bak"));
  fs.files["/network.tmp"] = std::make_shared<MemoryStream>();
  fs.files["/network.tmp"]->data = "{version:1,server:";
  NetworkPrefs loaded;
  ASSERT_TRUE(loadNetworkPrefs(fs, loaded));
  EXPECT_STREQ("192.168.1.1", loaded.server);
  ASSERT_TRUE(saveNetworkPrefs(fs, loaded));
}

TEST(NetworkPrefs, DefaultsAndCanonicalKeys) {
  NetworkPrefs p;
  EXPECT_TRUE(p.valid());
  EXPECT_TRUE(p.dhcp());
  EXPECT_TRUE(p.automatic());
  for (auto key : {"eth.mode", "eth.ip", "eth.gateway", "eth.subnet", "eth.dns", "ntp.server"})
    EXPECT_NE(nullptr, p.get(key));
  EXPECT_EQ(nullptr, p.get("eth.status")); // Status is a command, not a preference.
  EXPECT_EQ(nullptr, p.get("wifi.ip"));
}

TEST(NetworkPrefs, StaticSettingsRoundTripAndRejectionPreservesPreviousValue) {
  NetworkPrefs p;
  EXPECT_FALSE(p.set("eth.mode", "static"));
  ASSERT_TRUE(p.set("eth.ip", "192.168.1.50"));
  ASSERT_TRUE(p.set("eth.gateway", "192.168.1.1"));
  ASSERT_TRUE(p.set("eth.subnet", "255.255.255.0"));
  ASSERT_TRUE(p.set("eth.dns", "1.1.1.1"));
  ASSERT_TRUE(p.set("eth.mode", "static"));
  EXPECT_FALSE(p.set("eth.gateway", "192.168.2.1"));
  EXPECT_FALSE(p.set("eth.ip", "192.168.1.255"));
  EXPECT_FALSE(p.set("eth.subnet", "255.0.255.0"));
  EXPECT_STREQ("192.168.1.1", p.gateway);
  ASSERT_TRUE(p.set("ntp.server", "time.example.org"));
  MemoryStream stream;
  ASSERT_TRUE(p.saveSerial(stream));
  NetworkPrefs loaded;
  ASSERT_TRUE(loaded.loadSerial(stream)) << stream.data;
  EXPECT_TRUE(loaded.valid());
  EXPECT_FALSE(loaded.dhcp());
  EXPECT_FALSE(loaded.automatic());
  EXPECT_STREQ("192.168.1.50", loaded.ip);
  EXPECT_STREQ("time.example.org", loaded.server);
}

TEST(NetworkPrefs, RejectsMalformedAndOverlengthServers) {
  NetworkPrefs p;
  for (auto value : {"", "999.1.1.1", "1.2.3", "010.0.0.1", "0.0.0.00", "224.0.0.1", "127.0.0.1", "a..b", "-ntp.example", "ntp-.example", "name with spaces"})
    EXPECT_FALSE(p.set("ntp.server", value)) << value;
  EXPECT_FALSE(p.set("ntp.server", (std::string(60, 'a') + "." + std::string(40, 'b')).c_str()));
  EXPECT_STREQ("0.0.0.0", p.server);
  EXPECT_TRUE(p.set("ntp.server", "192.168.1.1"));
  EXPECT_TRUE(p.set("ntp.server", "0.0.0.0"));
  EXPECT_TRUE(p.automatic());
}

TEST(TimeSync, AutomaticDhcpPriorityAndPublicFallback) {
  TimeSyncPolicy p;
  EXPECT_EQ(p.DHCP, p.connect(100, true, true));
  EXPECT_EQ(p.NONE, p.tick(30099));
  EXPECT_EQ(p.PUBLIC, p.tick(30100));
  EXPECT_EQ(p.NONE, p.tick(60100));
  EXPECT_FALSE(p.attempting);
  EXPECT_EQ(p.DHCP, p.tick(90100));
  EXPECT_EQ(p.PUBLIC, p.connect(100000, true, false)); // Static or absent DHCP NTP.
}

TEST(TimeSync, ExclusiveServerNeverFallsBack) {
  TimeSyncPolicy p;
  EXPECT_EQ(p.OVERRIDE, p.connect(0, false, true));
  uint32_t now = 0;
  for (int i = 0; i < 100; ++i) {
    now += 30000;
    auto source = p.tick(now);
    EXPECT_TRUE(source == p.NONE || source == p.OVERRIDE);
    EXPECT_FALSE(p.synced);
    EXPECT_FALSE(p.canAdvert(1800000000));
  }
}

TEST(TimeSync, FirstSyncHoldoverAndFreshBoot) {
  TimeSyncPolicy p;
  EXPECT_FALSE(p.canAdvert(1800000000)); // Plausible retained/manual time is insufficient.
  p.connect(0, true, false);
  EXPECT_FALSE(p.success(10, 0));
  EXPECT_TRUE(p.success(20, 1800000000));
  EXPECT_TRUE(p.canAdvert(1800000000));
  p.advertised(1800000000);
  p.disconnect();
  EXPECT_TRUE(p.holdover);
  EXPECT_TRUE(p.canAdvert(1800000001));
  TimeSyncPolicy rebooted;
  EXPECT_FALSE(rebooted.canAdvert(1800000001));
}

TEST(TimeSync, BackwardCorrectionAndPeriodicRetry) {
  TimeSyncPolicy p;
  p.connect(0, true, true);
  ASSERT_TRUE(p.success(50, 1800000000));
  p.advertised(1800000000);
  EXPECT_EQ(p.NONE, p.tick(50 + p.SYNC_MS - 1));
  EXPECT_EQ(p.DHCP, p.tick(50 + p.SYNC_MS));
  ASSERT_TRUE(p.success(100 + p.SYNC_MS, 1799999900));
  EXPECT_FALSE(p.canAdvert(1799999901));
  EXPECT_FALSE(p.canAdvert(1800000000));
  EXPECT_TRUE(p.canAdvert(1800000001));
}

TEST(TimeSync, AttemptDeadlineSurvivesMillisWrap) {
  TimeSyncPolicy p;
  uint32_t boot = UINT32_MAX - 10000;
  p.connect(boot, true, true);
  EXPECT_EQ(p.NONE, p.tick(boot + p.ATTEMPT_MS - 1));
  EXPECT_EQ(p.PUBLIC, p.tick(boot + p.ATTEMPT_MS));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
