#pragma once

// This tree is the user build, and the flavour is fixed rather than selected.
//
// The development tree carries the diagnostics: a log file, ped and resource
// dumps, the event logger and the extra UI that reports frame cost and V8
// state. None of that is present here, and the flags below are pinned so a
// stray define cannot switch a code path back on that has no implementation to
// switch on to. The payload writes no log and produces no dump; the only files
// it touches are the user's own config and script library.
#define RAGEMP_DEV_BUILD 0

// The event logger instruments the game's V8, installs native dispatch hooks
// through a vectored exception handler and runs a disk writer. It is a research
// tool and its sources are not part of this tree.
#define RAGEMP_EVENT_LOGGER 0

// Surfaces that only make sense while working on the cheat: the per-server
// tools page and the frame-cost readouts in the status strip.
#define RAGEMP_DEV_UI 0
