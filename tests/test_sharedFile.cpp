// ibSharedFile / ibFileByteLock — a file several processes hold open, and a byte of it held.
//
// Two handles of one process stand for two processes here: a held byte is the handle's on Windows and on Linux (OFD
// locks). macOS has no OFD locks, and a process meets only other processes there — the holds' tests skip.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>

#include "core/fileSystem.h"
#include "core/guid.h"
#include "core/sharedFile.h"

namespace {

class SharedFileTest : public ::testing::Test {
protected:
	void SetUp() override
	{
		std::error_code error;
		m_path = ibTextOf(std::filesystem::temp_directory_path(error) / ibPathOf(ibT("oes-shared-") + ibNewUniqueGuid.str() + ibT(".bin")));
	}

	void TearDown() override
	{
		std::error_code error;
		std::filesystem::remove(ibPathOf(m_path), error);
	}

	ibString m_path;
};

TEST_F(SharedFileTest, WriteAt_ReadAt_RoundTrip)
{
	ibSharedFile file(m_path);
	ASSERT_TRUE(file.IsOpen());
	const std::uint64_t written = 0x0123456789ABCDEFull;
	ASSERT_TRUE(file.WriteAt(100, &written, sizeof(written)));
	ASSERT_TRUE(file.Flush());

	ibSharedFile other(m_path);
	ASSERT_TRUE(other.IsOpen());
	std::uint64_t read = 0;
	ASSERT_TRUE(other.ReadAt(100, &read, sizeof(read)));
	EXPECT_EQ(read, written);
}

// A file shorter than asked is not read as zeros.
TEST_F(SharedFileTest, ReadAt_PastTheEnd_Refused)
{
	ibSharedFile file(m_path);
	ASSERT_TRUE(file.IsOpen());
	const std::uint32_t written = 7;
	ASSERT_TRUE(file.WriteAt(0, &written, sizeof(written)));
	std::uint64_t read = 0;
	EXPECT_FALSE(file.ReadAt(0, &read, sizeof(read)));
}

TEST_F(SharedFileTest, Truncate_LosesWhatLayPast)
{
	ibSharedFile file(m_path);
	ASSERT_TRUE(file.IsOpen());
	const std::uint32_t written = 7;
	ASSERT_TRUE(file.WriteAt(1000, &written, sizeof(written)));
	ASSERT_TRUE(file.Truncate(10));
	std::uint32_t read = 0;
	EXPECT_FALSE(file.ReadAt(1000, &read, sizeof(read)));
	std::error_code error;
	EXPECT_EQ(std::filesystem::file_size(ibPathOf(m_path), error), 10u);
}

#ifndef __IBOSX__

TEST_F(SharedFileTest, ExclusiveByte_RefusesAnotherHandle_UntilReleased)
{
	ibSharedFile left(m_path);
	ibSharedFile right(m_path);
	ASSERT_TRUE(left.IsOpen());
	ASSERT_TRUE(right.IsOpen());

	ibFileByteLock held(left, 1000, ibFileByteLock::ibMode::Exclusive);
	ASSERT_TRUE(held.HoldsByte());
	EXPECT_FALSE(ibFileByteLock(right, 1000, ibFileByteLock::ibMode::Shared).HoldsByte());
	EXPECT_FALSE(ibFileByteLock(right, 1000, ibFileByteLock::ibMode::Exclusive).HoldsByte());
	EXPECT_TRUE(ibFileByteLock(right, 1001, ibFileByteLock::ibMode::Exclusive).HoldsByte());   // the next byte is free

	held.Release();
	EXPECT_TRUE(ibFileByteLock(right, 1000, ibFileByteLock::ibMode::Exclusive).HoldsByte());
}

TEST_F(SharedFileTest, SharedBytes_MeetEachOther_RefuseAnExclusive)
{
	ibSharedFile left(m_path);
	ibSharedFile right(m_path);
	ibSharedFile third(m_path);
	const ibFileByteLock a(left, 2000, ibFileByteLock::ibMode::Shared);
	const ibFileByteLock b(right, 2000, ibFileByteLock::ibMode::Shared);
	EXPECT_TRUE(a.HoldsByte());
	EXPECT_TRUE(b.HoldsByte());
	EXPECT_FALSE(ibFileByteLock(third, 2000, ibFileByteLock::ibMode::Exclusive).HoldsByte());
}

// ⭐ A HOLD GOES WITH ITS SCOPE, and with its file: what a lock file's owner gone leaves — nothing held.
TEST_F(SharedFileTest, Hold_GoesWithItsScope)
{
	ibSharedFile right(m_path);
	{
		ibSharedFile left(m_path);
		const ibFileByteLock held(left, 3000, ibFileByteLock::ibMode::Exclusive);
		ASSERT_TRUE(held.HoldsByte());
		EXPECT_FALSE(ibFileByteLock(right, 3000, ibFileByteLock::ibMode::Exclusive).HoldsByte());
	}
	EXPECT_TRUE(ibFileByteLock(right, 3000, ibFileByteLock::ibMode::Exclusive).HoldsByte());
}

// A wait for a held byte gets it once the holder lets go, within the time given.
TEST_F(SharedFileTest, Wait_GetsTheByteWhenLetGo)
{
	ibSharedFile left(m_path);
	ibSharedFile right(m_path);
	ibFileByteLock held(left, 4000, ibFileByteLock::ibMode::Exclusive);
	ASSERT_TRUE(held.HoldsByte());
	EXPECT_FALSE(ibFileByteLock(right, 4000, ibFileByteLock::ibMode::Exclusive, 50).HoldsByte());
	held.Release();
	EXPECT_TRUE(ibFileByteLock(right, 4000, ibFileByteLock::ibMode::Exclusive, 50).HoldsByte());
}

#endif

} // namespace
