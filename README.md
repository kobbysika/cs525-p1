# Project 1

- Name: Perry Aryee
- Email: perryaryee@u.boisestate.edu
- Class: CS525-001

## Known Bugs or Issues

None

## Experience

The main challenge was treating TCP as a stream instead of assuming each
`recv` call returns exactly one reply. Building a buffered line reader and
putting all I/O behind callbacks made that behavior easier to reason about and test. Testing each failure path also highlighted the importance of freeing allocated strings before returning from every stage of the session.
The callback-based transport was initially one of the less familiar parts of the project. After using it in the tests, its purpose became clearer: the SMTP session does not need to know whether its bytes come from a socket or an in-memory script. I also gained more practice with pointers, structures, function pointers, and handling errors in C.