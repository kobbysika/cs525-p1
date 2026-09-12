#ifndef LAB_H
#define LAB_H
#include <stddef.h>
#include <sys/types.h>
typedef ssize_t (*smtp_read_fn)(void *, void *, size_t);
typedef ssize_t (*smtp_write_fn)(void *, const void *, size_t);
typedef struct { smtp_read_fn read; smtp_write_fn write; void *context; } smtp_transport;
typedef struct { smtp_transport transport; char buffer[4096]; size_t start, end; } smtp_reader;
int smtp_reply_code(const char *line);
int smtp_reply_is_final(const char *line);
char *smtp_command(const char *verb, const char *argument);
char *smtp_dot_stuff(const char *body);
char *smtp_data_payload(const char *from, const char *to, const char *subject, const char *body);
int smtp_has_newline(const char *value);
int smtp_reader_init(smtp_reader *reader, smtp_transport transport);
int smtp_read_line(smtp_reader *reader, char **line);
int smtp_read_reply(smtp_reader *reader, int *code, char **reply);
int smtp_write_all(smtp_transport *transport, const char *data, size_t length);
int smtp_send_command(smtp_reader *reader, const char *command, int expected_code, char **reply);
int smtp_run_session(smtp_transport transport, const char *helo_host, const char *from,
                     const char *to, const char *subject, const char *body, char **error_message);
int smtp_socket_connect(const char *server, const char *port, char **error_message);
ssize_t smtp_socket_read(void *context, void *buffer, size_t length);
ssize_t smtp_socket_write(void *context, const void *buffer, size_t length);
#endif
