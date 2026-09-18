# Project 1

- Name: Perry Aryee
- Email: perryaryee@u.boisestate.edu
- Class: CS525-001

## Known Bugs or Issues

None

## Design

The program is organized into three layers so the SMTP protocol can be tested
without connecting to a live mail server.

### Pure protocol helpers

These functions parse SMTP reply codes, determine whether a reply line is
final, build command lines, normalize and dot-stuff the message body, and build
the complete DATA payload. They perform no input or output, which makes their
results deterministic and straightforward to unit test.

### Transport-independent session

The session layer runs the SMTP conversation: it reads the greeting and sends
`HELO`, `MAIL FROM`, `RCPT TO`, `DATA`, the message, and `QUIT`, checking every
reply before continuing. It performs all input and output through read and
write callback functions stored in `smtp_transport`. The buffered reader also
handles replies split across several reads, multiple lines received together,
and multiline SMTP replies. Unit tests can therefore supply an in-memory
scripted transport instead of using a network connection.

### Socket transport

The socket layer is a thin adapter for the real command-line client. It uses
`getaddrinfo` to resolve the server, attempts a TCP connection to the returned
addresses, and exposes `recv` and `send` through the same callbacks used by the
session layer. Keeping socket operations here prevents network details from
becoming part of the SMTP state machine.

## Experience

The main challenge was treating TCP as a stream instead of assuming each
`recv` call returns exactly one reply. Building a buffered line reader and
putting all I/O behind callbacks made that behavior easier to reason about and test. Testing each failure path also highlighted the importance of freeing allocated strings before returning from every stage of the session.
The callback-based transport was initially one of the less familiar parts of the project. After using it in the tests, its purpose became clearer: the SMTP session does not need to know whether its bytes come from a socket or an in-memory script. I also gained more practice with pointers, structures, function pointers, and handling errors in C.
