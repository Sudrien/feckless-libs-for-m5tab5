# feckless_mail

The outgoing mail account for the Tab5's apps, and a form to set it.

- `mailcfg.h`: the SMTP server, port, TLS or STARTTLS, login, app
  password, From and To, as one blob in NVS (`feckless_mail` namespace
  unless told otherwise), so a launcher's settings page and every app it
  runs use one account. Kept as `feckless_network_handler`'s saved
  networks are: in NVS, never on the card, and no encryption of its own --
  that is `CONFIG_NVS_ENCRYPTION` with flash encryption, a build's choice.
  `mailcfg_check()` says what a half-filled account lacks, in words for a
  person; `mailcfg_domain()` gives `@example.com` for a status line.
- `mailform.h`: the account as a form in a rectangle the app chooses, drawn
  with gfx: a row a field, Cancel and Save, and with `osk` the on-screen
  keyboard and a space bar. The app blits and hands it taps
  (`mailform_tap`) and USB keyboard characters (`mailform_key`); on
  `MAILFORM_SAVE` it stores the account with `mailcfg_save()`.
- `mailsend.h`: one plain-text message with that account, SMTP over TLS
  from the first byte or after STARTTLS, logged in with AUTH PLAIN or
  LOGIN, the server's certificate checked against IDF's CA bundle. A
  server that does not offer STARTTLS gets no password. Blocking, on a
  task with `MAILSEND_MIN_STACK` and the network up, bounded by one
  timeout; the result is a code and a sentence for a person.
  `mailsend_with()` (9009, 9010) sends files with it: the text still
  inline, each file attached after it, multipart/mixed.

No task and no hardware: mailcfg and mailform link into an app's ELF
(`ELF_COMPONENTS`). Their NVS calls are `feckless_nvs_handler`'s (the
ABI's nvs section), the drawing gfx's. `mailsend()` reaches lwIP and
Mbed TLS, which the ABI does not export, so for now it is a standalone
firmware's: an ELF app that calls it fails the ABI check, and one that
does not is unaffected.

```c
mailcfg_t c;
mailsend_result_t r;
if (mailcfg_load(NULL, &c) &&
    mailsend(&c, "Test", "It works.\n", 0, MAILSEND_TIMEOUT_MS, &r) != MAILSEND_OK) {
    show(r.why);                 /* "Login refused: 535 5.7.8 ..." */
}
mailcfg_wipe(&c);
```

```c
mailform_t f = { .x = 0, .y = 160, .w = 720, .row_h = 80, .scale = 3, .osk = true,
                 .bg = BG, .field = FIELD, .key = KEY, .text = TEXT, .dim = DIM,
                 .edge = EDGE, .go = GO, .bad = BAD, .on = ON };
mailcfg_t c;
mailcfg_load(NULL, &c);          /* or a half-finished one */
mailform_begin(&f, &c);
for (;;) {
    mailform_draw(&f);
    gfx_blit(f.y, f.y + mailform_h(&f));
    /* ...a tap at (x, y)... */
    mailform_result_t r = mailform_tap(&f, x, y);
    if (r == MAILFORM_CANCEL) break;
    if (r == MAILFORM_SAVE) { mailcfg_save(NULL, &f.c); break; }
}
mailform_done(&f);
mailcfg_wipe(&c);
```
