// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.


#include <map>
#include <string>
#include <utility>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/metrics/histogram_tester.h"
#include "build/build_config.h"
#include "net/base/net_errors.h"
#include "net/base/test_completion_callback.h"
#include "net/base/test_proxy_delegate.h"
#include "net/http/http_network_session.h"
#include "net/http/http_proxy_client_socket.h"
#include "net/http/http_proxy_connect_job.h"
#include "net/http/http_response_headers.h"
#include "net/log/net_log_with_source.h"
#include "net/nqe/network_quality_estimator_test_util.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/next_proto.h"
#include "net/socket/socket_tag.h"
#include "net/socket/socket_test_util.h"
#include "net/socket/socks_connect_job.h"
#include "net/socket/ssl_connect_job.h"
#include "net/socket/transport_client_socket_pool.h"
#include "net/socket/transport_connect_job.h"
#include "net/spdy/spdy_test_util_common.h"
#include "net/test/gtest_util.h"
#include "net/test/test_with_scoped_task_environment.h"
#include "net/third_party/quiche/src/spdy/core/spdy_protocol.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using net::test::IsError;
using net::test::IsOk;

namespace net {

namespace {

const int kMaxSockets = 32;
const int kMaxSocketsPerGroup = 6;
constexpr base::TimeDelta kUnusedIdleSocketTimeout =
    base::TimeDelta::FromSeconds(10);
const char * const kAuthHeaders[] = {
  "proxy-authorization", "Basic Zm9vOmJhcg=="
};
const int kAuthHeadersSize = base::size(kAuthHeaders) / 2;

enum HttpProxyType {
  HTTP,
  HTTPS,
  SPDY
};

const char kHttpProxyHost[] = "httpproxy.example.com";
const char kHttpsProxyHost[] = "httpsproxy.example.com";

}  // namespace

class HttpProxyClientSocketPoolTest
    : public ::testing::TestWithParam<HttpProxyType>,
      public WithScopedTaskEnvironment {
 protected:
  HttpProxyClientSocketPoolTest()
      : pool_(std::make_unique<TransportClientSocketPool>(
            kMaxSockets,
            kMaxSocketsPerGroup,
            kUnusedIdleSocketTimeout,
            &socket_factory_,
            session_deps_.host_resolver.get(),
            nullptr /* proxy_delegate */,
            session_deps_.cert_verifier.get(),
            session_deps_.channel_id_service.get(),
            session_deps_.transport_security_state.get(),
            session_deps_.cert_transparency_verifier.get(),
            session_deps_.ct_policy_enforcer.get(),
            nullptr /* ssl_client_session_cache */,
            nullptr /* ssl_client_session_cache_privacy_mode */,
            session_deps_.ssl_config_service.get(),
            nullptr /* socket_performance_watcher_factory */,
            &estimator_,
            nullptr /* net_log */)) {
    session_deps_.host_resolver->set_synchronous_mode(true);
    session_ = CreateNetworkSession();
  }

  ~HttpProxyClientSocketPoolTest() override = default;

  void InitPoolWithProxyDelegate(ProxyDelegate* proxy_delegate) {
    pool_ = std::make_unique<TransportClientSocketPool>(
        kMaxSockets, kMaxSocketsPerGroup, kUnusedIdleSocketTimeout,
        &socket_factory_, session_deps_.host_resolver.get(), proxy_delegate,
        session_deps_.cert_verifier.get(),
        session_deps_.channel_id_service.get(),
        session_deps_.transport_security_state.get(),
        session_deps_.cert_transparency_verifier.get(),
        session_deps_.ct_policy_enforcer.get(),
        nullptr /* ssl_client_session_cache */,
        nullptr /* ssl_client_session_cache_privacy_mode */,
        session_deps_.ssl_config_service.get(),
        nullptr /* socket_performance_watcher_factory */, &estimator_,
        nullptr /* net_log */);
  }

  void AddAuthToCache() {
    const base::string16 kFoo(base::ASCIIToUTF16("foo"));
    const base::string16 kBar(base::ASCIIToUTF16("bar"));
    GURL proxy_url(GetParam() == HTTP
                       ? (std::string("http://") + kHttpProxyHost)
                       : (std::string("https://") + kHttpsProxyHost));
    session_->http_auth_cache()->Add(proxy_url,
                                     "MyRealm1",
                                     HttpAuth::AUTH_SCHEME_BASIC,
                                     "Basic realm=MyRealm1",
                                     AuthCredentials(kFoo, kBar),
                                     "/");
  }

  scoped_refptr<TransportSocketParams> CreateHttpProxyParams() const {
    if (GetParam() != HTTP)
      return NULL;
    return new TransportSocketParams(HostPortPair(kHttpProxyHost, 80), false,
                                     OnHostResolutionCallback());
  }

  scoped_refptr<SSLSocketParams> CreateHttpsProxyParams() const {
    if (GetParam() == HTTP)
      return NULL;
    return new SSLSocketParams(
        new TransportSocketParams(HostPortPair(kHttpsProxyHost, 443), false,
                                  OnHostResolutionCallback()),
        NULL, NULL, HostPortPair(kHttpsProxyHost, 443), SSLConfig(),
        PRIVACY_MODE_DISABLED);
  }

  // Returns the a correctly constructed HttpProxyParms
  // for the HTTP or HTTPS proxy.
  scoped_refptr<TransportClientSocketPool::SocketParams> CreateParams(
      bool tunnel) {
    return TransportClientSocketPool::SocketParams::
        CreateFromHttpProxySocketParams(
            base::MakeRefCounted<HttpProxySocketParams>(
                CreateHttpProxyParams(), CreateHttpsProxyParams(),
                quic::QUIC_VERSION_UNSUPPORTED, std::string(),
                HostPortPair("www.google.com", tunnel ? 443 : 80),
                session_->http_auth_cache(),
                session_->http_auth_handler_factory(),
                session_->spdy_session_pool(), session_->quic_stream_factory(),
                /*is_trusted_proxy=*/false, tunnel,
                TRAFFIC_ANNOTATION_FOR_TESTS));
  }

  scoped_refptr<TransportClientSocketPool::SocketParams> CreateTunnelParams() {
    return CreateParams(true);
  }

  scoped_refptr<TransportClientSocketPool::SocketParams>
  CreateNoTunnelParams() {
    return CreateParams(false);
  }

  MockTaggingClientSocketFactory* socket_factory() { return &socket_factory_; }

  void Initialize(base::span<const MockRead> reads,
                  base::span<const MockWrite> writes,
                  base::span<const MockRead> spdy_reads,
                  base::span<const MockWrite> spdy_writes) {
    if (GetParam() == SPDY) {
      data_.reset(new SequencedSocketData(spdy_reads, spdy_writes));
    } else {
      data_.reset(new SequencedSocketData(reads, writes));
    }

    data_->set_connect_data(MockConnect(SYNCHRONOUS, OK));

    socket_factory()->AddSocketDataProvider(data_.get());

    if (GetParam() != HTTP) {
      ssl_data_.reset(new SSLSocketDataProvider(SYNCHRONOUS, OK));
      if (GetParam() == SPDY) {
        InitializeSpdySsl();
      }
      socket_factory()->AddSSLSocketDataProvider(ssl_data_.get());
    }
  }

  void InitializeSpdySsl() { ssl_data_->next_proto = kProtoHTTP2; }

  std::unique_ptr<HttpNetworkSession> CreateNetworkSession() {
    return SpdySessionDependencies::SpdyCreateSession(&session_deps_);
  }

  const base::HistogramTester& histogram_tester() { return histogram_tester_; }

 protected:
  MockTaggingClientSocketFactory socket_factory_;
  SpdySessionDependencies session_deps_;

  TestNetworkQualityEstimator estimator_;

  std::unique_ptr<HttpNetworkSession> session_;

  base::HistogramTester histogram_tester_;

  SpdyTestUtil spdy_util_;
  std::unique_ptr<SSLSocketDataProvider> ssl_data_;
  std::unique_ptr<SequencedSocketData> data_;
  std::unique_ptr<TransportClientSocketPool> pool_;
  ClientSocketHandle handle_;
  TestCompletionCallback callback_;
};

// All tests are run with three different proxy types: HTTP, HTTPS (non-SPDY)
// and SPDY.
INSTANTIATE_TEST_SUITE_P(HttpProxyType,
                         HttpProxyClientSocketPoolTest,
                         ::testing::Values(HTTP, HTTPS, SPDY));

TEST_P(HttpProxyClientSocketPoolTest, NeedAuth) {
  MockWrite writes[] = {
      MockWrite(ASYNC, 0,
                "CONNECT www.google.com:443 HTTP/1.1\r\n"
                "Host: www.google.com:443\r\n"
                "Proxy-Connection: keep-alive\r\n\r\n"),
  };
  MockRead reads[] = {
    // No credentials.
    MockRead(ASYNC, 1, "HTTP/1.1 407 Proxy Authentication Required\r\n"),
    MockRead(ASYNC, 2, "Proxy-Authenticate: Basic realm=\"MyRealm1\"\r\n"),
    MockRead(ASYNC, 3, "Content-Length: 10\r\n\r\n"),
    MockRead(ASYNC, 4, "0123456789"),
  };
  spdy::SpdySerializedFrame req(spdy_util_.ConstructSpdyConnect(
      NULL, 0, 1, LOW, HostPortPair("www.google.com", 443)));
  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_CANCEL));
  MockWrite spdy_writes[] = {
      CreateMockWrite(req, 0, ASYNC), CreateMockWrite(rst, 2, ASYNC),
  };
  spdy::SpdyHeaderBlock resp_block;
  resp_block[spdy::kHttp2StatusHeader] = "407";
  resp_block["proxy-authenticate"] = "Basic realm=\"MyRealm1\"";

  spdy::SpdySerializedFrame resp(
      spdy_util_.ConstructSpdyReply(1, std::move(resp_block)));
  MockRead spdy_reads[] = {CreateMockRead(resp, 1, ASYNC),
                           MockRead(ASYNC, 0, 3)};

  Initialize(reads, writes, spdy_reads, spdy_writes);

  int rv = handle_.Init(
      "a", CreateTunnelParams(), LOW, SocketTag(),
      ClientSocketPool::RespectLimits::ENABLED, callback_.callback(),
      ClientSocketPool::ProxyAuthCallback(), pool_.get(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());

  rv = callback_.WaitForResult();
  EXPECT_THAT(rv, IsError(ERR_PROXY_AUTH_REQUESTED));
  EXPECT_TRUE(handle_.is_initialized());
  ASSERT_TRUE(handle_.socket());
  ProxyClientSocket* tunnel_socket =
      static_cast<ProxyClientSocket*>(handle_.socket());
  if (GetParam() == SPDY) {
    EXPECT_TRUE(tunnel_socket->IsConnected());
    EXPECT_TRUE(tunnel_socket->IsUsingSpdy());
  } else {
    EXPECT_FALSE(tunnel_socket->IsConnected());
    EXPECT_FALSE(tunnel_socket->IsUsingSpdy());
  }
}

TEST_P(HttpProxyClientSocketPoolTest, HaveAuth) {
  // It's pretty much impossible to make the SPDY case behave synchronously
  // so we skip this test for SPDY
  if (GetParam() == SPDY)
    return;
  MockWrite writes[] = {
      MockWrite(SYNCHRONOUS, 0,
                "CONNECT www.google.com:443 HTTP/1.1\r\n"
                "Host: www.google.com:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };
  MockRead reads[] = {
    MockRead(SYNCHRONOUS, 1, "HTTP/1.1 200 Connection Established\r\n\r\n"),
  };

  Initialize(reads, writes, base::span<MockRead>(), base::span<MockWrite>());
  AddAuthToCache();

  int rv = handle_.Init(
      "a", CreateTunnelParams(), LOW, SocketTag(),
      ClientSocketPool::RespectLimits::ENABLED, callback_.callback(),
      ClientSocketPool::ProxyAuthCallback(), pool_.get(), NetLogWithSource());
  EXPECT_THAT(rv, IsOk());
  EXPECT_TRUE(handle_.is_initialized());
  ASSERT_TRUE(handle_.socket());
  EXPECT_TRUE(handle_.socket()->IsConnected());
}

TEST_P(HttpProxyClientSocketPoolTest, AsyncHaveAuth) {
  MockWrite writes[] = {
      MockWrite(ASYNC, 0,
                "CONNECT www.google.com:443 HTTP/1.1\r\n"
                "Host: www.google.com:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };
  MockRead reads[] = {
    MockRead(ASYNC, 1, "HTTP/1.1 200 Connection Established\r\n\r\n"),
  };

  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyConnect(kAuthHeaders, kAuthHeadersSize, 1, LOW,
                                      HostPortPair("www.google.com", 443)));
  MockWrite spdy_writes[] = {CreateMockWrite(req, 0, ASYNC)};
  spdy::SpdySerializedFrame resp(spdy_util_.ConstructSpdyGetReply(NULL, 0, 1));
  MockRead spdy_reads[] = {
      CreateMockRead(resp, 1, ASYNC),
      // Connection stays open.
      MockRead(SYNCHRONOUS, ERR_IO_PENDING, 2),
  };

  Initialize(reads, writes, spdy_reads, spdy_writes);
  AddAuthToCache();

  int rv = handle_.Init(
      "a", CreateTunnelParams(), LOW, SocketTag(),
      ClientSocketPool::RespectLimits::ENABLED, callback_.callback(),
      ClientSocketPool::ProxyAuthCallback(), pool_.get(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());

  EXPECT_THAT(callback_.WaitForResult(), IsOk());
  EXPECT_TRUE(handle_.is_initialized());
  ASSERT_TRUE(handle_.socket());
  EXPECT_TRUE(handle_.socket()->IsConnected());
}

TEST_P(HttpProxyClientSocketPoolTest, SslClientAuth) {
  if (GetParam() == HTTP)
    return;
  data_.reset(new SequencedSocketData());
  data_->set_connect_data(MockConnect(ASYNC, OK));
  socket_factory()->AddSocketDataProvider(data_.get());

  ssl_data_.reset(new SSLSocketDataProvider(ASYNC,
                                            ERR_SSL_CLIENT_AUTH_CERT_NEEDED));
  if (GetParam() == SPDY) {
    InitializeSpdySsl();
  }
  socket_factory()->AddSSLSocketDataProvider(ssl_data_.get());

  int rv = handle_.Init(
      "a", CreateTunnelParams(), LOW, SocketTag(),
      ClientSocketPool::RespectLimits::ENABLED, callback_.callback(),
      ClientSocketPool::ProxyAuthCallback(), pool_.get(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());

  EXPECT_THAT(callback_.WaitForResult(),
              IsError(ERR_SSL_CLIENT_AUTH_CERT_NEEDED));

  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());
  histogram_tester().ExpectTotalCount(
      "Net.HttpProxy.ConnectLatency.Secure.Error", 1);
  histogram_tester().ExpectTotalCount(
      "Net.HttpProxy.ConnectLatency.Insecure.Error", 0);
}

TEST_P(HttpProxyClientSocketPoolTest, TunnelSetupRedirect) {
  const std::string redirectTarget = "https://foo.google.com/";

  const std::string responseText = "HTTP/1.1 302 Found\r\n"
                                   "Location: " + redirectTarget + "\r\n"
                                   "Set-Cookie: foo=bar\r\n"
                                   "\r\n";
  MockWrite writes[] = {
      MockWrite(ASYNC, 0,
                "CONNECT www.google.com:443 HTTP/1.1\r\n"
                "Host: www.google.com:443\r\n"
                "Proxy-Connection: keep-alive\r\n"
                "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n"),
  };
  MockRead reads[] = {
    MockRead(ASYNC, 1, responseText.c_str()),
  };
  spdy::SpdySerializedFrame req(
      spdy_util_.ConstructSpdyConnect(kAuthHeaders, kAuthHeadersSize, 1, LOW,
                                      HostPortPair("www.google.com", 443)));
  spdy::SpdySerializedFrame rst(
      spdy_util_.ConstructSpdyRstStream(1, spdy::ERROR_CODE_CANCEL));

  MockWrite spdy_writes[] = {
      CreateMockWrite(req, 0, ASYNC), CreateMockWrite(rst, 3, ASYNC),
  };

  const char* const responseHeaders[] = {
    "location", redirectTarget.c_str(),
    "set-cookie", "foo=bar",
  };
  const int responseHeadersSize = base::size(responseHeaders) / 2;
  spdy::SpdySerializedFrame resp(spdy_util_.ConstructSpdyReplyError(
      "302", responseHeaders, responseHeadersSize, 1));
  MockRead spdy_reads[] = {
      CreateMockRead(resp, 1, ASYNC), MockRead(ASYNC, 0, 2),
  };

  Initialize(reads, writes, spdy_reads, spdy_writes);
  AddAuthToCache();

  int rv = handle_.Init(
      "a", CreateTunnelParams(), LOW, SocketTag(),
      ClientSocketPool::RespectLimits::ENABLED, callback_.callback(),
      ClientSocketPool::ProxyAuthCallback(), pool_.get(), NetLogWithSource());
  EXPECT_THAT(rv, IsError(ERR_IO_PENDING));
  EXPECT_FALSE(handle_.is_initialized());
  EXPECT_FALSE(handle_.socket());

  rv = callback_.WaitForResult();

  if (GetParam() == HTTP) {
    // We don't trust 302 responses to CONNECT from HTTP proxies.
    EXPECT_THAT(rv, IsError(ERR_TUNNEL_CONNECTION_FAILED));
    EXPECT_FALSE(handle_.is_initialized());
    EXPECT_FALSE(handle_.socket());
  } else {
    // Expect ProxyClientSocket to return the proxy's response, sanitized.
    EXPECT_THAT(rv, IsError(ERR_HTTPS_PROXY_TUNNEL_RESPONSE_REDIRECT));
    EXPECT_TRUE(handle_.is_initialized());
    ASSERT_TRUE(handle_.socket());

    const ProxyClientSocket* tunnel_socket =
        static_cast<ProxyClientSocket*>(handle_.socket());
    const HttpResponseInfo* response = tunnel_socket->GetConnectResponseInfo();
    const HttpResponseHeaders* headers = response->headers.get();

    // Make sure Set-Cookie header was stripped.
    EXPECT_FALSE(headers->HasHeader("set-cookie"));

    // Make sure Content-Length: 0 header was added.
    EXPECT_TRUE(headers->HasHeaderValue("content-length", "0"));

    // Make sure Location header was included and correct.
    std::string location;
    EXPECT_TRUE(headers->IsRedirect(&location));
    EXPECT_EQ(location, redirectTarget);
  }
}

// Test that SocketTag passed into HttpProxyClientSocketPool is applied to
// returned underlying TCP sockets.
#if defined(OS_ANDROID)
TEST_P(HttpProxyClientSocketPoolTest, Tag) {
  // Socket tagging only supports Android without data reduction proxy, so only
  // HTTP proxies are supported.
  if (GetParam() != HTTP)
    return;
  Initialize(base::span<MockRead>(), base::span<MockWrite>(),
             base::span<MockRead>(), base::span<MockWrite>());
  SocketTag tag1(SocketTag::UNSET_UID, 0x12345678);
  SocketTag tag2(getuid(), 0x87654321);

  // Verify requested socket is tagged properly.
  int rv = handle_.Init(
      "a", CreateNoTunnelParams(), LOW, tag1,
      ClientSocketPool::RespectLimits::ENABLED, CompletionOnceCallback(),
      ClientSocketPool::ProxyAuthCallback(), pool_.get(), NetLogWithSource());
  EXPECT_THAT(rv, IsOk());
  EXPECT_TRUE(handle_.is_initialized());
  ASSERT_TRUE(handle_.socket());
  EXPECT_TRUE(handle_.socket()->IsConnected());
  EXPECT_EQ(socket_factory()->GetLastProducedTCPSocket()->tag(), tag1);
  EXPECT_TRUE(
      socket_factory()->GetLastProducedTCPSocket()->tagged_before_connected());

  // Verify reused socket is retagged properly.
  StreamSocket* socket = handle_.socket();
  handle_.Reset();
  rv = handle_.Init(
      "a", CreateNoTunnelParams(), LOW, tag2,
      ClientSocketPool::RespectLimits::ENABLED, CompletionOnceCallback(),
      ClientSocketPool::ProxyAuthCallback(), pool_.get(), NetLogWithSource());
  EXPECT_THAT(rv, IsOk());
  EXPECT_TRUE(handle_.socket());
  EXPECT_TRUE(handle_.socket()->IsConnected());
  EXPECT_EQ(handle_.socket(), socket);
  EXPECT_EQ(socket_factory()->GetLastProducedTCPSocket()->tag(), tag2);
  handle_.socket()->Disconnect();
  handle_.Reset();
}

TEST_P(HttpProxyClientSocketPoolTest, TagWithProxy) {
  // Socket tagging only supports Android without data reduction proxy, so only
  // HTTP proxies are supported.
  if (GetParam() != HTTP)
    return;
  std::string request =
      "CONNECT www.google.com:443 HTTP/1.1\r\n"
      "Host: www.google.com:443\r\n"
      "Proxy-Connection: keep-alive\r\n"
      "Proxy-Authorization: Basic Zm9vOmJhcg==\r\n\r\n";
  MockWrite writes[] = {
      MockWrite(SYNCHRONOUS, 0, request.c_str()),
  };
  MockRead reads[] = {
      MockRead(SYNCHRONOUS, 1, "HTTP/1.1 200 Connection Established\r\n\r\n"),
  };

  Initialize(reads, writes, base::span<MockRead>(), base::span<MockWrite>());
  AddAuthToCache();

  SocketTag tag1(SocketTag::UNSET_UID, 0x12345678);
  SocketTag tag2(getuid(), 0x87654321);

  // Verify requested socket is tagged properly.
  int rv = handle_.Init(
      "a", CreateTunnelParams(), LOW, tag1,
      ClientSocketPool::RespectLimits::ENABLED, CompletionOnceCallback(),
      ClientSocketPool::ProxyAuthCallback(), pool_.get(), NetLogWithSource());
  EXPECT_THAT(rv, IsOk());
  EXPECT_TRUE(handle_.is_initialized());
  ASSERT_TRUE(handle_.socket());
  EXPECT_TRUE(handle_.socket()->IsConnected());
  EXPECT_EQ(socket_factory()->GetLastProducedTCPSocket()->tag(), tag1);
  EXPECT_TRUE(
      socket_factory()->GetLastProducedTCPSocket()->tagged_before_connected());

  // Verify reused socket is retagged properly.
  StreamSocket* socket = handle_.socket();
  handle_.Reset();
  rv = handle_.Init(
      "a", CreateNoTunnelParams(), LOW, tag2,
      ClientSocketPool::RespectLimits::ENABLED, CompletionOnceCallback(),
      ClientSocketPool::ProxyAuthCallback(), pool_.get(), NetLogWithSource());
  EXPECT_THAT(rv, IsOk());
  EXPECT_TRUE(handle_.socket());
  EXPECT_TRUE(handle_.socket()->IsConnected());
  EXPECT_EQ(handle_.socket(), socket);
  EXPECT_EQ(socket_factory()->GetLastProducedTCPSocket()->tag(), tag2);
  handle_.socket()->Disconnect();
  handle_.Reset();
}
#endif

}  // namespace net
