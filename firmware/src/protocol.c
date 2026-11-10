#include "protocol.h"
#include "regs.h"
#include "ui.h"
#include "heater.h"

/* ---------- receiving: interrupt into a ring buffer ---------- */
static volatile uint8_t rx_buffer[256];
static volatile uint8_t rx_head, rx_tail;
static uint32_t commands, errors;

void USART0_IRQHandler(void)
{
    uint32_t sr = USART_SR;
    if (sr & ((1u << 5) | (1u << 3))) {            /* RXNE or overrun: reading DR clears both */
        uint8_t c = (uint8_t)USART_DR;
        uint8_t n = (uint8_t)(rx_head + 1u);
        if (n != rx_tail) { rx_buffer[rx_head] = c; rx_head = n; }
    }
}

/* ---------- sending ---------- */
static void put_char(char c) { while (!(USART_SR & (1u << 7))) ; USART_DR = (uint8_t)c; }

static char line[96];
static unsigned length;

static void append(const char *s) { while (*s && length < sizeof(line) - 4u) line[length++] = *s++; }

static void number(int32_t v)
{
    char b[12]; int i = 0; uint32_t u;
    if (v < 0) { append("-"); u = (uint32_t)(-v); } else u = (uint32_t)v;
    do { b[i++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    char r[12]; int j = 0;
    while (i) r[j++] = b[--i];
    r[j] = 0;
    append(r);
}

static const char HEX[] = "0123456789ABCDEF";

static void send_line(void)                      /* line[0] is '$' */
{
    uint8_t x = 0;
    for (unsigned i = 1; i < length; i++) x ^= (uint8_t)line[i];
    line[length++] = '*';
    line[length++] = HEX[x >> 4];
    line[length++] = HEX[x & 15u];
    for (unsigned i = 0; i < length; i++) put_char(line[i]);
    put_char('\r'); put_char('\n');
    length = 0;
}

void protocol_send_status(const chamber_status_t s[2])
{
    for (int k = 0; k < 2; k++) {
        const chamber_status_t *q = &s[k];
        length = 0;
        append("$K,"); number(k);
        append(","); number(q->running);
        append(","); number(q->setpoint);
        append(","); number(q->duration_min);
        append(","); number((int32_t)q->remaining_s);
        append(","); number(q->material);
        append(","); number(q->t);
        append(","); number(q->rh);
        append(","); number(q->ntc);
        append(","); number(q->power);
        append(","); number(q->fault);
        append(","); number(q->fan);
        append(","); number(q->sensor_ok);
        send_line();
    }
}

void protocol_send_version(uint32_t v)
{
    length = 0;
    append("$V,"); number((int32_t)(v >> 16)); append("."); number((int32_t)((v >> 8) & 255u));
    append("."); number((int32_t)(v & 255u));
    send_line();
}

static void reply(int ok, const char *command, const char *extra)
{
    length = 0;
    append(ok ? "$OK," : "$ERR,"); append(command); append(","); append(extra);
    send_line();
    if (!ok) errors++;
}

/* ---------- commands ---------- */
static char rx_line[48];
static unsigned rx_length;
static uint8_t rx_discard;

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int equal(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == 0 && *b == 0; }

static int read_number(const char *s, int32_t *v)
{
    int32_t r = 0; int n = 0;
    if (!*s) return 0;
    while (*s) {
        if (*s < '0' || *s > '9' || ++n > 6) return 0;
        r = r * 10 + (*s++ - '0');
    }
    *v = r;
    return 1;
}

static void execute(char *z, uint32_t t)
{
    /* z = "!COMMAND,k[,value]*HH" without line ending */
    char *star = 0;
    for (char *p = z; *p; p++) if (*p == '*') star = p;
    if (!star || hex_value(star[1]) < 0 || hex_value(star[2]) < 0 || star[3]) { errors++; return; }
    uint8_t x = 0;
    for (char *p = z + 1; p < star; p++) x ^= (uint8_t)*p;
    if (x != (uint8_t)(hex_value(star[1]) * 16 + hex_value(star[2]))) { errors++; return; }
    *star = 0;

    char *part[4] = { z + 1, 0, 0, 0 };
    int n = 1;
    for (char *p = z + 1; *p && n < 4; p++) if (*p == ',') { *p = 0; part[n++] = p + 1; }
    const char *command = part[0];
    int32_t k, value = 0;
    if (n < 2 || !read_number(part[1], &k) || k > 1) { reply(0, command, "KAMMER"); return; }
    if (n >= 3 && !read_number(part[2], &value)) { reply(0, command, "WERT"); return; }
    commands++;

    int r;
    if (equal(command, "START"))       r = remote_start((int)k, t);
    else if (equal(command, "STOP"))   r = remote_stop((int)k, t);
    else if (equal(command, "SOLL"))   r = n >= 3 ? remote_setpoint((int)k, (int)value, t) : 1;
    else if (equal(command, "DAUER"))  r = n >= 3 ? remote_duration((int)k, (int)value, t) : 1;
    else if (equal(command, "MAT"))    r = n >= 3 ? remote_material((int)k, (int)value, t) : 1;
    else if (equal(command, "QUIT"))   { heater_acknowledge((int)k); r = heater[k].fault ? 2 : 0; }
    else { reply(0, command, "UNBEKANNT"); return; }

    static const char *REASON[] = { "", "WERT", "FEHLER", "LAEUFT" };
    if (r == 0) { char kk[2] = { (char)('0' + k), 0 }; reply(1, command, kk); }
    else reply(0, command, REASON[r < 4 ? r : 1]);
}

void protocol_commands(uint32_t t)
{
    while (rx_tail != rx_head) {
        char c = (char)rx_buffer[rx_tail];
        rx_tail = (uint8_t)(rx_tail + 1u);
        if (c == '\r') continue;
        if (c == '\n') {
            rx_line[rx_length] = 0;
            if (!rx_discard && rx_length && rx_line[0] == '!') execute(rx_line, t);
            rx_length = 0; rx_discard = 0;
            continue;
        }
        if (rx_length < sizeof(rx_line) - 1u) rx_line[rx_length++] = c;
        else rx_discard = 1;                       /* too long: discard the whole line */
    }
}

uint32_t protocol_commands_total(void) { return commands; }
uint32_t protocol_errors_total(void) { return errors; }
