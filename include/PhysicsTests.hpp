#pragma once

// Headless physics regression suite. Returns the number of failed checks
// (0 = all passed). Used by both `GravityEngine --test` and the GravityTests
// executable registered with CTest.
int runPhysicsTests();
