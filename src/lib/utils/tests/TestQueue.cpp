#include "gtest/gtest.h"
#include "queue.h"

TEST(TestQueue, pushPop)
{
    Queue<char*> q;

    ASSERT_EQ(q.peek(), nullptr);

    char d = '1';

    q.push(&d);

    ASSERT_EQ(q.peek(), &d);

    char* x = q.pop();

    ASSERT_EQ(x, &d);
}
