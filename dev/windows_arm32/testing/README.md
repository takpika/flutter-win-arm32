These files support native Windows SDK verification inside the Ubuntu CI
container. The Wine executables stay under `_work`, outside the SDK payload.
They do not change Flutter's batch scripts, the application, or the compiler.

Wine 11's command interpreter rejects copies from handles above 2 and closes
new handles above 2 immediately. Flutter's unchanged `shared.bat` uses handle
3 to restore stderr and handle 9 to hold its exclusive bootstrap lock. The
scoped Wine patch keeps logical handles through the redirected command,
exposes saved standard streams, and restores the caller's handles on exit.
The build retains the upstream command sources, icons, and translations.

The bootstrap check first verifies actual stderr forwarding and restoration,
an actual Windows sharing violation while the lock is held, and successful
opening after the lock is released. Normal Flutter startup and SDK relocation
remain separate checks. Passing these runtime checks does not establish a
complete SDK application build or release readiness.
