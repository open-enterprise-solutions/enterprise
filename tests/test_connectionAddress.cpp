// AN ADDRESS A CLIENT IS STARTED ON — the cluster's, oes://server[:port]/<base>, and the link a browser shows for a
// web server's publication, http(s)://server[:port]/<base>/…: behind both the same door, /<base>/client, and the same
// packets (protocol/connectionServer.h, webserver/webServer.h). What the address is read as decides where the socket
// goes; every case here is a link a person would paste.

#include <gtest/gtest.h>

#include "core/net/ports.h"                  // ibPort::Clients — the port the cluster's address leaves out
#include "protocol/connectionServer.h"

namespace {

struct ibReadAddress {
	bool m_read = false;
	ibString m_host;
	int m_port = 0;
	ibString m_base;
	bool m_secure = false;
};

ibReadAddress Read(const ibString& address)
{
	ibReadAddress read;
	read.m_read = ibProtocolConnectionServer::ParseAddress(address, read.m_host, read.m_port, read.m_base, read.m_secure);
	return read;
}

} // namespace

TEST(ConnectionAddress, TheClusterPortLeftOutIsTheClusters)
{
	const ibReadAddress read = Read(ibT("oes://appserver/trade"));
	ASSERT_TRUE(read.m_read);
	EXPECT_EQ(read.m_host, ibT("appserver"));
	EXPECT_EQ(read.m_port, ibPort::Clients.m_first);
	EXPECT_EQ(read.m_base, ibT("trade"));
	EXPECT_FALSE(read.m_secure);

	const ibReadAddress secure = Read(ibT("oess://appserver:7400/trade/"));
	ASSERT_TRUE(secure.m_read);
	EXPECT_EQ(secure.m_port, 7400);
	EXPECT_EQ(secure.m_base, ibT("trade"));
	EXPECT_TRUE(secure.m_secure);
}

TEST(ConnectionAddress, AnHttpsLinkIsTheBrowsersPortOverTls)
{
	const ibReadAddress read = Read(ibT("https://203.0.113.7.sslip.io/fb_test_buh/"));
	ASSERT_TRUE(read.m_read);
	EXPECT_EQ(read.m_host, ibT("203.0.113.7.sslip.io"));
	EXPECT_EQ(read.m_port, 443);
	EXPECT_EQ(read.m_base, ibT("fb_test_buh"));
	EXPECT_TRUE(read.m_secure);
}

TEST(ConnectionAddress, AnHttpLinkKeepsItsPortAndDropsThePagesTail)
{
	const ibReadAddress read = Read(ibT("http://127.0.0.1:8080/trade/index.html?x=1#y"));
	ASSERT_TRUE(read.m_read);
	EXPECT_EQ(read.m_host, ibT("127.0.0.1"));
	EXPECT_EQ(read.m_port, 8080);
	EXPECT_EQ(read.m_base, ibT("trade"));
	EXPECT_FALSE(read.m_secure);

	const ibReadAddress bare = Read(ibT("http://webhost/trade"));
	ASSERT_TRUE(bare.m_read);
	EXPECT_EQ(bare.m_port, 80);
	EXPECT_EQ(bare.m_base, ibT("trade"));
}

TEST(ConnectionAddress, ALinksEscapedNameIsReadBack)
{
	// "торг" as the browser's address bar gives it — its UTF-8 bytes escaped.
	const ibReadAddress read = Read(ibT("http://webhost:8080/%D1%82%D0%BE%D1%80%D0%B3/"));
	ASSERT_TRUE(read.m_read);
	EXPECT_EQ(read.m_base, ibString::FromUTF8("\xD1\x82\xD0\xBE\xD1\x80\xD0\xB3"));
	// …and written again as the door's path, the same bytes.
	EXPECT_EQ(ibProtocolConnectionServer::ClientPath(read.m_base), "/%D1%82%D0%BE%D1%80%D0%B3/client");
}

TEST(ConnectionAddress, WhatIsNotABasesAddressIsRefused)
{
	EXPECT_FALSE(Read(ibT("http://webhost")).m_read);             // no base
	EXPECT_FALSE(Read(ibT("http://webhost/")).m_read);            // an empty one
	EXPECT_FALSE(Read(ibT("http://webhost/%ZZ/")).m_read);        // an escape that is none
	EXPECT_FALSE(Read(ibT("http://webhost/a%2Fb/")).m_read);      // a name with a slash in it
	EXPECT_FALSE(Read(ibT("http://webhost:0/trade/")).m_read);    // no port
	EXPECT_FALSE(Read(ibT("ftp://webhost/trade/")).m_read);       // not a scheme of the protocol
	EXPECT_FALSE(Read(ibT("webhost/trade")).m_read);
}
