
Do sprawdzenia:
- czy da się wywołać lokalne przrwanie mboxa?
- nowe device tree czy działa i da się z niego wyciągnąć wspólną konfigurację, n.p.
  - `ICMSG_VERSION_1_SUPPORTED` - if icmsg 1 or icbmsg 1 or compat
  - `ICMSG_VERSION_2_SUPPORTED` - if icmsg 2 or icbmsg 2
  - `ICMSG_VERSION_BOTH_SUPPORTED`
  - `ICMSG_ISR_CALLBACKS_ENABLED`
  - `ICMSG_DEDICATED_THREAD_ENABLED`
  - `ICMSG_SHARED_THREAD_ENABLED`
  - `ICMSG_SYSTEM_WORK_QUEUE_ENABLED`
  - `ICMSG_SHARED_THREAD_STACK_SIZE`
  - `ICMSG_SHARED_THREAD_PRIORITY`
  - `ICMSG_SHORT_MESSAGES_ENABLED` (if at least one ICBMsg endpoint instance)
  - `ICMSG_LONG_MESSAGES_ENABLED` (if at least one ICMsg endpoint instance)
  - `ICBMSG_VERSION` (1 or 2)


Device tree entries:

ICMsg:
```dts
# OLD
  tx-region: phandle
  rx-region: phandle
  dcache-alignment: int
  mboxes:
  mbox-names:
# OLD - ICBMsg only
  tx-blocks: int
  rx-blocks: int
# NEW
  version: enum [1, 2] - version number supported (2 by default)
  compatibility: boolean - does compatibility with version 1 should be supported (yes by default),
    for ICBMsg default is "no", and it will show error if set to yes.
  thread: enum (default "shared")
    "none" - no thread to call callbacks, all callbacks are called directly from the interrupt (may be useful for nRF RPC)
    "dedicated" - this instance has its own dedicated thread to call callbacks
    "shared" - this instance uses thread shared between all instances of ICMsg and ICBMsg
    "system" - this instance uses system work queue to call the callbacks
  thread-stack-size: int - size of the stack, depends on "thread" argument (default: 1024)
    thread=="none" or "system" - ignored
    thread=="dedicated" - size of stack for dedicated thread
    thread=="shared" - minimum stack size required for shared thread
  thread-priority: int - callback thread priority, depends on "thread" argument
    thread=="none" or "system" - ignored
    thread=="dedicated" - priority of dedicated thread
    thread=="shared" - lowest priority required for shared thread
```

