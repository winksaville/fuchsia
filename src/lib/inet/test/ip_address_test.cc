// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/inet/ip_address.h"

#include "gtest/gtest.h"

namespace inet {
namespace test {

// Tests the properties of an invalid address.
TEST(IpAddressTest, Invalid) {
  IpAddress under_test;
  EXPECT_FALSE(under_test.is_valid());
  EXPECT_EQ(AF_UNSPEC, under_test.family());
  EXPECT_FALSE(under_test.is_v4());
  EXPECT_FALSE(under_test.is_v6());
  EXPECT_FALSE(under_test.is_loopback());
  EXPECT_EQ("<invalid>", under_test.ToString());
  EXPECT_EQ(IpAddress::kInvalid, under_test);
}

// Tests the properties of a V4 address.
TEST(IpAddressTest, V4) {
  IpAddress under_test(1, 2, 3, 4);
  EXPECT_TRUE(under_test.is_valid());
  EXPECT_EQ(AF_INET, under_test.family());
  EXPECT_TRUE(under_test.is_v4());
  EXPECT_FALSE(under_test.is_v6());
  EXPECT_FALSE(under_test.is_loopback());
  EXPECT_EQ(0x04030201u, under_test.as_in_addr().s_addr);
  EXPECT_EQ(0x04030201u, under_test.as_in_addr_t());
  EXPECT_EQ(1u, under_test.as_bytes()[0]);
  EXPECT_EQ(2u, under_test.as_bytes()[1]);
  EXPECT_EQ(3u, under_test.as_bytes()[2]);
  EXPECT_EQ(4u, under_test.as_bytes()[3]);
  EXPECT_EQ(0x0201u, under_test.as_words()[0]);
  EXPECT_EQ(0x0403u, under_test.as_words()[1]);
  EXPECT_EQ(4u, under_test.byte_count());
  EXPECT_EQ(2u, under_test.word_count());
  EXPECT_EQ("1.2.3.4", under_test.ToString());
}

// Tests the properties of a V6 address.
TEST(IpAddressTest, V6) {
  IpAddress under_test(0x0001, 0x0203, 0x0405, 0x0607, 0x0809, 0x0a0b, 0x0c0d, 0x0e0f);
  EXPECT_TRUE(under_test.is_valid());
  EXPECT_EQ(AF_INET6, under_test.family());
  EXPECT_FALSE(under_test.is_v4());
  EXPECT_TRUE(under_test.is_v6());
  EXPECT_FALSE(under_test.is_loopback());

  for (size_t i = 0; i < 16; ++i) {
    EXPECT_EQ(i, under_test.as_in6_addr().s6_addr[i]);
    EXPECT_EQ(i, under_test.as_bytes()[i]);
  }

  for (size_t i = 0; i < 8; ++i) {
    EXPECT_EQ((i * 2u) + 256u * (i * 2u + 1u), under_test.as_words()[i]);
  }

  EXPECT_EQ(16u, under_test.byte_count());
  EXPECT_EQ(8u, under_test.word_count());
  EXPECT_EQ("1:203:405:607:809:a0b:c0d:e0f", under_test.ToString());
}

// Tests constructors.
TEST(IpAddressTest, Constructors) {
  IpAddress v4(1, 2, 3, 4);
  IpAddress v6(0x1234, 0, 0, 0, 0, 0, 0, 0x5678);

  EXPECT_EQ(v4, IpAddress(v4.as_in_addr_t()));
  EXPECT_EQ(v4, IpAddress(v4.as_in_addr()));
  sockaddr_storage sockaddr_v4{.ss_family = AF_INET};
  memcpy(reinterpret_cast<uint8_t*>(&sockaddr_v4) + sizeof(sa_family_t), v4.as_bytes(),
         v4.byte_count());
  EXPECT_EQ(v4, IpAddress(reinterpret_cast<sockaddr*>(&sockaddr_v4)));
  EXPECT_EQ(v4, IpAddress(sockaddr_v4));
  fuchsia::net::IpAddress fn_ip_address_v4;
  fn_ip_address_v4.set_ipv4(fuchsia::net::Ipv4Address{.addr = {1, 2, 3, 4}});
  EXPECT_EQ(v4, IpAddress(&fn_ip_address_v4));

  EXPECT_EQ(v6, IpAddress(0x1234, 0x5678));
  EXPECT_EQ(v6, IpAddress(v6.as_in6_addr()));
  sockaddr_storage sockaddr_v6{.ss_family = AF_INET6};
  memcpy(reinterpret_cast<uint8_t*>(&sockaddr_v6) + sizeof(sa_family_t), v6.as_bytes(),
         v6.byte_count());
  EXPECT_EQ(v6, IpAddress(reinterpret_cast<sockaddr*>(&sockaddr_v6)));
  EXPECT_EQ(v6, IpAddress(sockaddr_v6));
  fuchsia::net::IpAddress fn_ip_address_v6;
  fn_ip_address_v6.set_ipv6(fuchsia::net::Ipv6Address{
      .addr = {0x12, 0x34, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x56, 0x78}});
  EXPECT_EQ(v6, IpAddress(&fn_ip_address_v6));
}

// Tests is_loopback method.
TEST(IpAddressTest, IsLoopback) {
  EXPECT_FALSE(IpAddress::kInvalid.is_loopback());
  EXPECT_FALSE(IpAddress(1, 2, 3, 4).is_loopback());
  EXPECT_FALSE(IpAddress(0x1234, 0x5678).is_loopback());
  EXPECT_TRUE(IpAddress::kV4Loopback.is_loopback());
  EXPECT_TRUE(IpAddress::kV6Loopback.is_loopback());
}

// Tests FromString static method.
TEST(IpAddressTest, FromString) {
  EXPECT_EQ(IpAddress(1, 2, 3, 4), IpAddress::FromString("1.2.3.4"));
  EXPECT_EQ(IpAddress(1, 2, 3, 4), IpAddress::FromString("001.002.003.004"));
  EXPECT_EQ(IpAddress(0, 0, 0, 0), IpAddress::FromString("0.0.0.0"));
  EXPECT_EQ(IpAddress(255, 255, 255, 255), IpAddress::FromString("255.255.255.255"));

  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1.2"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1.2.3"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1.2.3.4.5"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1.2.3.4.5.6"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("0001.2.3.4"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1.2.3..4"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1.2.3.4."));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString(".1.2.3.4"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("256.2.3.4"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1234.2.3.4"));

  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08),
            IpAddress::FromString("1:2:3:4:5:6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08),
            IpAddress::FromString("1::3:4:5:6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0, 0x04, 0x05, 0x06, 0x07, 0x08),
            IpAddress::FromString("1:2::4:5:6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0, 0x05, 0x06, 0x07, 0x08),
            IpAddress::FromString("1:2:3::5:6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0, 0x06, 0x07, 0x08),
            IpAddress::FromString("1:2:3:4::6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0, 0x07, 0x08),
            IpAddress::FromString("1:2:3:4:5::7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0, 0x08),
            IpAddress::FromString("1:2:3:4:5:6::8"));
  EXPECT_EQ(IpAddress(0x01, 0, 0, 0x04, 0x05, 0x06, 0x07, 0x08),
            IpAddress::FromString("1::4:5:6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0, 0, 0, 0x05, 0x06, 0x07, 0x08), IpAddress::FromString("1::5:6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0, 0, 0, 0, 0x06, 0x07, 0x08), IpAddress::FromString("1::6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0, 0, 0, 0, 0, 0x07, 0x08), IpAddress::FromString("1::7:8"));
  EXPECT_EQ(IpAddress(0x01, 0, 0, 0, 0, 0, 0, 0x08), IpAddress::FromString("1::8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0, 0, 0x05, 0x06, 0x07, 0x08),
            IpAddress::FromString("1:2::5:6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0, 0, 0, 0x06, 0x07, 0x08), IpAddress::FromString("1:2::6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0, 0, 0, 0, 0x07, 0x08), IpAddress::FromString("1:2::7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0, 0, 0, 0, 0, 0x08), IpAddress::FromString("1:2::8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0, 0, 0x06, 0x07, 0x08),
            IpAddress::FromString("1:2:3::6:7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0, 0, 0, 0x07, 0x08), IpAddress::FromString("1:2:3::7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0, 0, 0, 0, 0x08), IpAddress::FromString("1:2:3::8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0, 0, 0x07, 0x08),
            IpAddress::FromString("1:2:3:4::7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0, 0, 0, 0x08), IpAddress::FromString("1:2:3:4::8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0, 0, 0x07, 0x08),
            IpAddress::FromString("1:2:3:4::7:8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0, 0, 0, 0x08), IpAddress::FromString("1:2:3:4::8"));
  EXPECT_EQ(IpAddress(0x01, 0x02, 0x03, 0x04, 0x05, 0, 0, 0x08),
            IpAddress::FromString("1:2:3:4:5::8"));
  EXPECT_EQ(IpAddress(0x1234, 0x5678, 0x9abc, 0xdef0, 0x0fed, 0xcba9, 0x8765, 0x4321),
            IpAddress::FromString("1234:5678:9abc:def0:0fed:cba9:8765:4321"));
  EXPECT_EQ(IpAddress(0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff),
            IpAddress::FromString("ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"));

  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("::1"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1::"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:::2"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1::2::3"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString(":1::2"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1::2:"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("00000::ffff"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("0000::fffff"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3:4:5:6:7:8:9"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3:4:5:6:7:8:"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString(":1:2:3:4:5:6:7:8"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1::2:3:4:5:6:7:8"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2::3:4:5:6:7:8"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3::4:5:6:7:8"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3:4::5:6:7:8"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3:4:5::6:7:8"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3:4:5:6::7:8"));
  EXPECT_EQ(IpAddress::kInvalid, IpAddress::FromString("1:2:3:4:5:6:7::8"));
}

// Tests FromString and ToString against each other.
TEST(IpAddressTest, StringRoundTrip) {
  for (size_t i = 0; i < 1000; ++i) {
    in_addr addr;
    zx_cprng_draw(&addr, sizeof(addr));
    IpAddress v4(addr);
    EXPECT_EQ(v4, IpAddress::FromString(v4.ToString()));

    in6_addr addr6;
    zx_cprng_draw(&addr6, sizeof(addr6));
    IpAddress v6(addr6);
    EXPECT_EQ(v6, IpAddress::FromString(v6.ToString()));
  }
}

}  // namespace test
}  // namespace inet
