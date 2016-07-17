#include "gtest/gtest.h"
#include "notify.h"

TEST(TestNotify, basicTest)
{
    Notify notify;

    ASSERT_TRUE(notify.notifyFD() > 0);

    int fd = notify.notifyFD();
    int ret = fcntl(fd, F_SETFL, O_NONBLOCK);
    ASSERT_EQ(ret, 0);

    char b;
    auto rc = read(fd, &b, sizeof(b));

    ASSERT_EQ(rc, -1);
    ASSERT_EQ(errno, EAGAIN);

    notify.notify();

    rc = read(fd, &b, sizeof(b));

    ASSERT_EQ(rc, 1);
}
