#pragma once

// Which half of the tree this build is.
//
// One switch decides it, SHADOW_DEV in CMake, which defines RAGEMP_DIAGNOSTICS.
// The flags below follow it rather than being set by hand, so there is no way to
// end up with a build that is half one thing and half the other.
//
// The user build writes no log and produces no dump; the only files it touches
// are the user's own config and script library.

// Ped and resource dumps and the rest of the development tree's own surfaces.
// Pinned off even in a developer build here: those branches belong to a
// different tree and have no implementation in this one, so switching this on
// compiles code that cannot link.
#define RAGEMP_DEV_BUILD 0

// The event logger instruments the game's V8, installs native dispatch hooks
// through a vectored exception handler and runs a disk writer. Its sources live
// in src/Dev and are not published with the user tree, so this follows the same
// switch that decides whether src/Dev is compiled at all.
#if defined(RAGEMP_DIAGNOSTICS)
#  define RAGEMP_EVENT_LOGGER 1
#else
#  define RAGEMP_EVENT_LOGGER 0
#endif

// Surfaces that only make sense while working on the cheat: the per-server tools
// page and the frame-cost readouts in the status strip. The tools themselves are
// part of this tree; this is what makes their page reachable.
#if defined(RAGEMP_DIAGNOSTICS)
#  define RAGEMP_DEV_UI 1
#else
#  define RAGEMP_DEV_UI 0
#endif
