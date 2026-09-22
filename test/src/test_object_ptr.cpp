// Copyright (c) 2021-2025, Nikita Pennie <nikitapnn1@gmail.com>
// SPDX-License-Identifier: MIT

// ObjectPtr reference counting, against a counting stand-in for a proxy:
// every add_ref must be matched by exactly one release.

#include <gtest/gtest.h>

#include <nprpc/object_ptr.hpp>

namespace {

struct Counts {
  int add_refs = 0;
  int releases = 0;
};

/// Minimal proxy: ObjectPtr only needs add_ref() and release().
struct FakeObject {
  Counts* counts;
  int refs = 1; // the reference ObjectPtr adopts on construction

  void add_ref()
  {
    ++counts->add_refs;
    ++refs;
  }
  void release()
  {
    ++counts->releases;
    --refs;
  }
};

using Ptr = nprpc::ObjectPtr<FakeObject>;

TEST(ObjectPtr, AdoptsWithoutAddingAReference)
{
  Counts c;
  FakeObject obj{&c};
  {
    Ptr p(&obj);
    EXPECT_EQ(c.add_refs, 0);
  }
  EXPECT_EQ(c.releases, 1);
  EXPECT_EQ(obj.refs, 0);
}

TEST(ObjectPtr, CopyAssignmentReleasesThePreviousObject)
{
  Counts ca, cb;
  FakeObject a{&ca}, b{&cb};
  {
    Ptr pa(&a);
    Ptr pb(&b);
    pa = pb;
    // `a` lost its only holder, `b` gained one.
    EXPECT_EQ(a.refs, 0);
    EXPECT_EQ(b.refs, 2);
    EXPECT_EQ(pa.get(), &b);
  }
  EXPECT_EQ(a.refs, 0);
  EXPECT_EQ(b.refs, 0);
}

TEST(ObjectPtr, MoveAssignmentReleasesThePreviousObject)
{
  Counts ca, cb;
  FakeObject a{&ca}, b{&cb};
  {
    Ptr pa(&a);
    Ptr pb(&b);
    pa = std::move(pb);
    EXPECT_EQ(a.refs, 0);
    EXPECT_EQ(b.refs, 1); // moved, not shared
    EXPECT_EQ(pa.get(), &b);
    EXPECT_FALSE(pb);
  }
  EXPECT_EQ(b.refs, 0);
}

TEST(ObjectPtr, SelfAssignmentKeepsTheObject)
{
  Counts c;
  FakeObject obj{&c};
  {
    Ptr p(&obj);
    Ptr& alias = p; // defeats -Wself-assign-overloaded
    p = alias;
    EXPECT_EQ(p.get(), &obj);
    EXPECT_EQ(obj.refs, 1);
    p = std::move(alias);
    EXPECT_EQ(p.get(), &obj);
    EXPECT_EQ(obj.refs, 1);
  }
  EXPECT_EQ(obj.refs, 0);
}

TEST(ObjectPtr, AssigningAnEmptyHandleReleases)
{
  Counts c;
  FakeObject obj{&c};
  Ptr p(&obj);
  p = Ptr();
  EXPECT_FALSE(p);
  EXPECT_EQ(obj.refs, 0);
}

} // namespace
