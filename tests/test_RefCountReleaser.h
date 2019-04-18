// Testsuite for evio::RefCountReleaser.
//
// TestObject is derived from AIRefCount with the usual
// characteristic:
//
// 1) It can only be created on the heap (with operator new).
// 2) It will self-destruct as soon as the last boost::intrusive_ptr<TestObject> that
//    points to the object is destructed.

#include "evio/RefCountReleaser.h"
#include <cassert>

class ExpectDeletionOf;

class TestObject : public AIRefCount
{
 private:
  friend class ExpectDeletionOf;
  ExpectDeletionOf* m_expect_deletion;

 public:
  TestObject() : m_expect_deletion(nullptr) { }
  ~TestObject() { test_destruction(); }

  evio::RefCountReleaser release();
  evio::RefCountReleaser release2();

 private:
  void test_destruction();
};

class ExpectDeletionOf
{
 private:
  friend class TestObject;
  bool m_initialized;
  bool m_destructed;

 public:
  ExpectDeletionOf() : m_initialized(false), m_destructed(false) { }
  ExpectDeletionOf(TestObject* test_object) : m_initialized(true), m_destructed(false) { test_object->m_expect_deletion = this; }
  ~ExpectDeletionOf() { test_destruction(); }

  void operator=(boost::intrusive_ptr<TestObject> const& test_object) { assert(m_initialized == false); m_initialized = true; test_object->m_expect_deletion = this; }
  void operator=(TestObject* test_object) { assert(m_initialized == false); m_initialized = true; test_object->m_expect_deletion = this; }

 private:
  void test_destruction()
  {
    // Call the assignment operator before destructing this object.
    assert(m_initialized);
    ASSERT_TRUE(m_destructed) << "Expected TestObject to be deleted. Actual: it was not deleted.";
  }
};

void TestObject::test_destruction()
{
  ASSERT_TRUE(m_expect_deletion) << "Unexpected deletion of TestObject.";
  m_expect_deletion->m_destructed = true;
}

TEST(RefCountReleaser, LeavingScopeWithDestruction)
{
  ExpectDeletionOf expect_deletion_of;
  {
    boost::intrusive_ptr<TestObject> test_object = new TestObject;
    expect_deletion_of = test_object;
  } // TestObject should be deleted now.
}   // Correct deletion tested here.

TEST(RefCountReleaser, LeavingScopeWithoutDestruction)
{
  ExpectDeletionOf expect_deletion_of;
  {
    boost::intrusive_ptr<TestObject> keep_alive;
    {
      boost::intrusive_ptr<TestObject> test_object = new TestObject;
      keep_alive = test_object;
    } // No deletion occurs here.
    expect_deletion_of = keep_alive;
  } // TestObject should be deleted now.
}   // Correct deletion tested here.

TEST(RefCountReleaser, DefaultConstructor)
{
  // Fix the testsuite when this changes.
  assert(sizeof(evio::RefCountReleaser) == sizeof(AIRefCount*));

  // This basically does nothing.
  {
    evio::RefCountReleaser rcr;
    ASSERT_FALSE(rcr);
    rcr.execute();      // Does nothing.
    ASSERT_FALSE(rcr);
  }
}

TEST(RefCountReleaser, Reset)
{
  ExpectDeletionOf expect_deletion_of;
  {
    boost::intrusive_ptr<TestObject> test_object = new TestObject;

    evio::RefCountReleaser rcr;
    rcr = test_object.get();
    ASSERT_TRUE(rcr);
    rcr.reset();              // Sets the internal pointer to nullptr.
    ASSERT_FALSE(rcr);

    expect_deletion_of = test_object;
  } // TestObject should be deleted now.
}   // Correct deletion tested here.

TEST(RefCountReleaser, RefCountReleaserOutOfScope)
{
  ExpectDeletionOf expect_deletion_of;
  {
    // This is NOT how one should use RefCountReleaser.
    evio::RefCountReleaser rcr;
    TestObject* test_object = new TestObject;
    rcr = test_object;
    intrusive_ptr_add_ref(test_object); // Increment ref count from 0 to 1.
    expect_deletion_of = test_object;
  } // TestObject should be deleted because rcr goes out of scope, decrementing the ref count from 1 to 0.
}   // Correct deletion tested here.

TEST(RefCountReleaser, SimpleAssignment)
{
  ExpectDeletionOf expect_deletion_of;
  TestObject* test_object = new TestObject;
  {
    evio::RefCountReleaser rcr;
    {
      boost::intrusive_ptr<TestObject> ptr = test_object;       // Assign to boost::intrusive_ptr before calling inhibit_deletion()!
      ptr->inhibit_deletion();
      rcr = test_object;
    } // Deletion prevented by call to inhibit_deletion().
    expect_deletion_of = test_object;
  } // TestObject should be deleted because rcr goes out of scope.
}   // Correct deletion tested here.

evio::RefCountReleaser TestObject::release()
{
  evio::RefCountReleaser needs_release;
  needs_release = this;
  return needs_release;                 // Move constructor needs to exist, but isn't used.
}

TEST(RefCountReleaser, AssignmentInFunction)
{
  ExpectDeletionOf expect_deletion_of;
  TestObject* test_object = new TestObject;
  {
    evio::RefCountReleaser rcr;
    {
      boost::intrusive_ptr<TestObject> ptr = test_object;
      ptr->inhibit_deletion();
      rcr = test_object->release();     // Move-assignment.
    } // Deletion prevented by call to inhibit_deletion().
    expect_deletion_of = test_object;
  } // TestObject should be deleted because rcr goes out of scope.
}   // Correct deletion tested here.

evio::RefCountReleaser TestObject::release2()
{
  evio::RefCountReleaser needs_release;
  needs_release = this;
  evio::RefCountReleaser needs_release2{std::move(needs_release)};      // Explicitly test the move constructor.
  return needs_release2;
}

TEST(RefCountReleaser, MoveConstructor)
{
  ExpectDeletionOf expect_deletion_of;
  TestObject* test_object = new TestObject;
  {
    evio::RefCountReleaser rcr;
    {
      boost::intrusive_ptr<TestObject> ptr = test_object;
      ptr->inhibit_deletion();
      rcr = test_object->release2();
    } // Deletion prevented by call to inhibit_deletion().
    expect_deletion_of = test_object;
  } // TestObject should be deleted because rcr goes out of scope.
}   // Correct deletion tested here.

TEST(RefCountReleaser, Execute)
{
  TestObject* test_object = new TestObject;
  evio::RefCountReleaser rcr;
  {
    boost::intrusive_ptr<TestObject> ptr = test_object;
    ptr->inhibit_deletion();
    rcr = test_object->release();
  } // Deletion prevented by call to inhibit_deletion().
  {
    ExpectDeletionOf expect_deletion_of;
    expect_deletion_of = test_object;
    rcr.execute(); // TestObject should be deleted now.
  }                // Correct deletion tested here.
}                  // Going out of scope of rcr doesn't do anything anymore.
