
# MongoDB Assertions and You #

Have you ever wondered about the difference between all of the different assertion macros? You have
come to the right place. This page will provide some brief background on the different assertion
types and attempt to provide some guidance on selecting the right one. Lets get started!

## The Macros ##

### Fatalities ###

Sometimes, the best thing to do is to give up. No, really! MongoDB server processes typically are
deployed as a fault tolerant distributed system. While taking down a node is always disruptive,
doing so is always superior to corrupting or destroying data. If during program execution a
situation is detected where the program cannot safely continue, terminating the process is the best
choice. We have several macros that do just that. In order of preference, they are:

#### invariant ####
An invariant is a tool for developers to catch bugs by asserting something is true. If the assertion
fails, a backtrace is logged and the process is terminated. There is an optional second argument to
give a message when this happens. If not provided, the body of the invariant serves as the message.
You should use invariant to protect regions of code from assumptions or abuse. Typically, you are
being defensive: you as the developer believe that the stated condition should not be possible or
permitted, but you cannot prove that it is impossible, and it is more important to terminate the
process in a non-descriptive manner than allow the program to proceed in a potentially corrupt
state. Typically, an invariant failure should indicate a logic error in the code itself. An
invariant failure almost always represents a bug in our code, but a tripped fassert may not be.

#### fassert ####
An fassert is a fatal assertion, intended for cases where recovery is not possible, there is not a
bug, and operator actionable context is available. It is preferred over the other fatal assertions
because it provides the most detail. An fassert must be accompanied by a text description and a
unique error code. Providing this information makes it much easier for crash reports originating
with our support and field teams to be quickly correlated to a likely code location, no matter what
version of the product is being run. See below for details on the unique error codes. A good example
of when to use fassert would be when data corruption is detected. You cannot safely proceed (data is
already corrupt), but you can provide end-user useful information about what was detected and how to
attempt to recover.

#### dassert ####
A dassert is a debug-only invariant. It will only fire in builds where debugging logic has been
enabled by building the source code with --dbg#on. There are two valid cases for using dassert. The
first is if you would have liked to have written invariant, but the runtime cost to do the check is
simply too high for a production build. This should be very rare, because in effect you are counting
on our DEBUG builders on evergreen to save you. If you find yourself putting things in dassert for
"performance reasons", take a step back and see if there is another way to achieve what you want.
The second place where dassert is useful is when you have a self-contained subsystem and you believe
that you have a proof that the condition really cannot happen with the code as currently written. In
such cases, adding dasserts can be useful for future developers working in the subsystem so that if
they change the code in ways that invalidate the proof, then they are alerted to the fact that
either their code is making an erroneous assumption, or the proofs are no longer valid given the new
behavior of the code.

In general, be skeptical about dassert and use it sparingly, if at all.

#### verify (DEPRECATED) ####
The verify macro is an old bit of machinery that we don't like much anymore. It means, more or less
invariant in debug builds, but throws an exception in non-debug builds. We no longer write new
instances of verify, and hopefully one day there will be none left in the codebase. You might wonder
why we don't switch them all over to invariant (or, the other way, over to dassert). However, since
we can't know the intention of the author on a case-by-case basis, we can't know just from looking
at the verify call whether the author actually believed the condition could occur. So, don't write
verify, use one of the other macros.

### User Facing Errors ###

Not all things are fatal (fortunately!). Sometimes, you have reached a situation where you cannot
proceed because either the user has made an error (failed, say, to provide a required field on a
command), or where an operation must be failed (an operation timeout has expired). In such cases, it
would be clearly wrong to terminate the server. Instead, an error should be surfaced back to the
"user" (the shell, a driver, etc.). The following macros are used for this purpose:

#### uassert ####
A uassert is much like an fassert, in that it has both an error code (either unique or symbolic) and
a message. However, unlike fassert, it is not process fatal: it simply throws an exception up to the
top level of the server command dispatch logic, which then issues a reply back to the connected
client with the relevant error information. You should write uassert when a user facing operation
must/should be failed.

#### massert (DISCOURAGED) ####
The massert macro is exactly like uassert, except that a log entry is written with the details. It
is used infrequently, and probably is headed for removal at some point. The -ed variation is
inconsistently spelled msgasserted (see below section on variations).

### Legacy ###

#### wassert (DELETED) ####
A warning assertion. It doesn't exist on master anymore, and is listed here only for completion
because it may appear on some older branches. It logged a message but allowed execution to continue.
It has been replaced with log() and warning().

#### assert (BANNED) ####
We do not permit the use of the C++ assert macro.

### Variations ###

#### ...ed and fassertFailed ####
You may find variations of the above assertions, such as fassertFailed rather than fassert, or
uasserted rather than uassert. These forms of the assertions do not take a condition to be checked,
and simply unconditionally invoke the behavior of the basic assertion types failure case. These are
useful if you have already checked the condition in surrounding logic, and simply want to obtain the
same behavior as if you had written the other macro.

#### ...StatusOK ####
Many of our functions return or the Status type. The StatusOK variants of assertion macros (like
uassertStatusOK) take a status object and automatically check if the state is OK. If not, the
assertion behavior is triggered. These are useful to avoid needing to explicitly write Status state
checks when the compiler can do it for you. When passed a StatusWith<T>, these variants typically
return T in the status-is-ok case.

#### ...StatusOKWithContext ####
Like the above, but provides the caller the opportunity to add additional context. For example, you
may receive a non-OK status from executing a query and want to preserve that error code. However,
you know there's useful context here like "this happened while attempting to contact this host" that
the user would be interested in.

#### ??? ####
The above list of variations is far from exhaustive, but you can usually figure out the intention
from the type of arguments passed, and the base name. What do you suppose
fassertFailedWithStatusNoTrace does?

## Discussion ##
So, after all of that, what macros should I use? In my opinion, you can mostly rely on uassert,
fassert, invariant, and maybe sometimes dassert, along with their variations. Ignore all the others
when writing new code, and try to migrate to these when you are modernizing subsystem that use older
macros. 

 

## FAQ ##

* Q) I disagree with something written above, or think it could be improved.

  A) Build agreement for your change or improvement and apply it.


* Q) I saw a macro that isn't on this list. I'm panicking and sweating all over my keyboard in
     terror.

  A) Don't Panic! Ask around. Someone knows what it does. Then come back here and update this page
     once you have found out.

