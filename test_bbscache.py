#!/usr/bin/env python3
"""
BBSCache parser simulation — mirrors the C++ state machine logic.
Feed it a raw BBS session string and see exactly what gets detected/stored.

Usage:
    python3 test_bbscache.py
    python3 test_bbscache.py < mysession.txt
"""

import re
import sys

# ── Regex patterns (mirrors BBSCache.cpp) ─────────────────────────────────────

RX_BANNER1    = re.compile(r"Connected to BBS")
RX_BANNER2    = re.compile(r"\*+ Connected to (\S+)")
RX_BPQ        = re.compile(r"\[BPQ-")
RX_BANNER_NODE= re.compile(r"\b(\w+):(\S+)\}")           # Pattern 4: NODENAME:CALL}
RX_BANNER_CONN= re.compile(r"\bConnected to ([A-Z][A-Z0-9]+-\d+)")  # Pattern 3b
RX_PROMPT     = re.compile(r"de ([A-Z0-9/\-]+)>")
RX_LIST_LINE  = re.compile(
    r"^\s*(\d+)\s+"           # msg_id
    r"(\d{1,2}-\w{3})\s+"     # date
    r"([A-Z][$FNKP])\s+"      # type  (B$, BF, BN, BK, BP)
    r"(\d+)\s+"               # size
    r"(\S+)\s+"               # category
    r"@(\S+)\s+"              # dist
    r"(\S+)\s+"               # from_call
    r"(.+)$"                  # title
)
RX_MSG_END    = re.compile(r"\[End of Message #(\d+) from (\S+)\]")

# ── States ─────────────────────────────────────────────────────────────────────

IDLE = "Idle"
CONNECTED = "Connected"
AT_PROMPT = "AtPrompt"
PARSING_LIST = "ParsingList"
READING_MSG = "ReadingMessage"
COMPOSING = "ComposingMessage"

# ── Helpers ────────────────────────────────────────────────────────────────────

def detect_banner(line):
    """Returns (matched, nodeCall)"""
    m = re.search(r"(\w+):(\S+)\}\s*Connected to BBS", line)
    if m: return True, m.group(2)

    m = RX_BANNER2.search(line)
    if m: return True, m.group(1)

    if RX_BANNER1.search(line): return True, ""

    m = RX_BANNER_CONN.search(line)
    if m: return True, m.group(1)

    m = RX_BANNER_NODE.search(line)
    if m: return True, m.group(2)

    if RX_BPQ.search(line): return True, ""

    return False, ""

def detect_prompt(line):
    if RX_PROMPT.search(line): return True
    if line.strip() == ">": return True
    return False

def parse_list_line(line):
    m = RX_LIST_LINE.match(line)
    if not m: return None
    return {
        "msg_id":    int(m.group(1)),
        "date":      m.group(2),
        "type":      m.group(3),
        "size":      int(m.group(4)),
        "category":  m.group(5),
        "dist":      m.group(6),
        "from_call": m.group(7),
        "title":     m.group(8).strip(),
    }

# ── State machine ──────────────────────────────────────────────────────────────

def simulate(session_text):
    state       = IDLE
    node_call   = ""
    bulletins   = []
    msg_accum   = ""
    cur_msg_id  = 0

    lines = []
    for raw in session_text.replace('\r\n', '\n').replace('\r', '\n').split('\n'):
        clean = ''.join(c for c in raw if c == '\n' or (' ' <= c < '\x7f'))
        lines.append(clean)

    print(f"{'─'*60}")
    print(f"  BBSCache simulation — {len(lines)} lines")
    print(f"{'─'*60}")

    for lineno, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue

        # ── Re-detection (any non-Idle state) ──────────────────────────────
        if state != IDLE:
            matched, new_call = detect_banner(line)
            if matched and new_call and new_call != node_call:
                print(f"  L{lineno:3d}  RE-DETECT  new node={new_call!r} (was {node_call!r})")
                state     = CONNECTED
                node_call = new_call
                continue

        # ── Switch on state ─────────────────────────────────────────────────
        if state in (IDLE, CONNECTED):
            matched, nc = detect_banner(line)
            if matched:
                state = CONNECTED
                if nc and nc != node_call:
                    node_call = nc
                    print(f"  L{lineno:3d}  BANNER     node_call={node_call!r}")
                else:
                    print(f"  L{lineno:3d}  BANNER     (no call extracted, state→Connected)")

            if state == CONNECTED and detect_prompt(line):
                pm = RX_PROMPT.search(line)
                if not node_call and pm:
                    node_call = pm.group(1)
                print(f"  L{lineno:3d}  PROMPT     node={node_call!r} → AtPrompt ✓")
                state = AT_PROMPT

        elif state == AT_PROMPT:
            pass  # commands handled separately (see simulate_command)

        elif state == PARSING_LIST:
            if detect_prompt(line):
                print(f"  L{lineno:3d}  END-LIST   prompt detected → AtPrompt")
                state = AT_PROMPT
            else:
                b = parse_list_line(line)
                if b:
                    bulletins.append(b)
                    print(f"  L{lineno:3d}  BULLETIN   #{b['msg_id']}  {b['date']}  {b['type']}  {b['category']}  {b['from_call']}  {b['title'][:40]}")
                else:
                    print(f"  L{lineno:3d}  NO-MATCH   {line!r}")

        elif state == READING_MSG:
            m = RX_MSG_END.search(line)
            if m:
                print(f"  L{lineno:3d}  MSG-END    #{m.group(1)} ({len(msg_accum)} chars) → AtPrompt")
                msg_accum  = ""
                cur_msg_id = 0
                state = AT_PROMPT
            elif detect_prompt(line):
                print(f"  L{lineno:3d}  MSG-END    prompt (no end marker) → AtPrompt")
                msg_accum  = ""
                cur_msg_id = 0
                state = AT_PROMPT
            else:
                msg_accum += line + "\n"

        elif state == COMPOSING:
            if detect_prompt(line):
                print(f"  L{lineno:3d}  COMPOSE-END prompt → AtPrompt")
                state = AT_PROMPT

    print(f"{'─'*60}")
    print(f"  Final state : {state}")
    print(f"  Node        : {node_call!r}")
    print(f"  Bulletins   : {len(bulletins)} parsed")
    print(f"{'─'*60}")
    return bulletins

def simulate_command(cmd, state, node_call):
    """Show what onCommandSent would do."""
    t = cmd.strip().upper()
    new_state = state
    msg = ""
    if state == AT_PROMPT:
        if t.startswith("LL") or t.startswith("LB") or t.startswith("LM") or \
           t.startswith("LT") or t.startswith("LP") or t == "L" or t.startswith("L "):
            new_state = PARSING_LIST
            msg = "→ ParsingList"
        elif t.startswith("R "):
            try:
                mid = int(t[2:].strip())
                new_state = READING_MSG
                msg = f"→ ReadingMessage #{mid}"
            except ValueError:
                msg = "(bad msg id)"
        elif t.startswith("SP ") or t.startswith("SB ") or t.startswith("SR "):
            new_state = COMPOSING
            msg = "→ ComposingMessage"
        else:
            msg = "(unrecognized, state unchanged)"
    else:
        msg = f"(state is {state}, not AtPrompt — ignored)"
    return new_state, msg

# ── Built-in test sessions ─────────────────────────────────────────────────────

N9SEO_SESSION = """\
Connecting...
Connected to N9SEO-1 500 Mode
[BPQ-6.0.25.23-B2FWIHJM$]
Please enter your Name
>
Sylvain
Hello Sylvain. Latest Message is 52260, Last listed is 0
Please enter your Home BBS using the Home command.
You may also enter your QTH and ZIP/Postcode using qth and zip commands.
de N9SEO>
QTH Montreal, QC
QTH is Montreal, QC
de N9SEO>
Home VA2OPS.#MTL.QC.CAN.NOAM
HomeBBS is VA2OPS.#MTL.QC.CAN.NOAM
de N9SEO>
ll 5
52257 21-Apr B$ 1689 EQUAKE @WW CX2SA (M5.6) off the east coast of Honshu, Japan 40.2 143.5
52256 21-Apr B$ 1358 JNOS2 @WW I0OJJ decode jnos-2.0q1-dev.txz
52255 21-Apr B$ 5077 NEWS @WW LU9DCE ONALLBANDS 21-APR
52253 21-Apr B$ 689 JNOS2 @WW KQ6UP RE:7+ jnos-2.0q1-dev.txz 5/5 <+7Plus
52252 21-Apr B$ 4645 AGCW @WW DJ6UX EuCW Snakes & Ladders 2026/03
de N9SEO>
"""

N3MEL_SESSION = """\
attach 1
MTLNDE:VA2OPS-7} Ok
BW500
VARA} OK
C N3MEL-2
VARA} BITRATE (1) 18 bps TX
*** Connected to N3MEL-2
[BPQ-6.0.25.13-B1FWIHJM$]
de N3MEL>
LL 3
16020 20-Apr BF 591 JNOS2 @WW W6TJ RE^2:jnos updates and stuff
16019 20-Apr BF 288 JNOS2 @WW K5DAT RE:jnos updates and stuff
16018 20-Apr BF 1588 WILDLF @WW KC2NJV WILDLIFE NEWS
de N3MEL>
"""

# ── Main ───────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    if len(sys.argv) > 1:
        # Read from file: python3 test_bbscache.py session.txt
        with open(sys.argv[1]) as f:
            text = f.read()
        print(f"\n=== Session from {sys.argv[1]} ===")
        simulate(text)
        sys.exit(0)
    elif not sys.stdin.isatty():
        # Read from pipe: cat session.txt | python3 test_bbscache.py
        text = sys.stdin.read()
        if text.strip():
            print("\n=== Custom session from stdin ===")
            simulate(text)
            sys.exit(0)

    # No input — run built-in test sessions
    if True:
        # Run built-in test sessions
        print("\n=== N9SEO-1 (new node, bare > prompt) ===")
        # Simulate commands inline since we process data+commands together
        # For accuracy, commands typed by user are handled via simulate_command
        state = IDLE
        node_call = ""

        # Split into data received vs commands sent
        # Lines starting with known user commands are treated as sent commands
        USER_CMDS = {"SYLVAIN", "QTH", "HOME", "LL", "LM", "LB", "R ", "BYE", "SP ", "SB ", "SR ", "ATTACH", "BW", "C "}
        lines_n9seo = N9SEO_SESSION.strip().split('\n')

        # For simplicity, run the full simulate (commands embedded)
        # The simulate function processes all lines as received data
        # Then we note which lines would have been sent by user
        bulletins = simulate(N9SEO_SESSION)

        print("\n  NOTE: Lines like 'Sylvain', 'QTH ...', 'll 5' are USER COMMANDS")
        print("  In the real app, onCommandSent() handles those separately.")
        print("  The state machine above shows how received DATA is processed.")
        print()

        # Now simulate correctly with command interleaving
        print("=== N9SEO-1 — CORRECT simulation (data + commands) ===")

        state = IDLE
        node_call = ""
        stored = []

        RECEIVED = [
            ("data", "Connecting..."),
            ("data", "Connected to N9SEO-1 500 Mode"),
            ("data", "[BPQ-6.0.25.23-B2FWIHJM$]"),
            ("data", "Please enter your Name"),
            ("data", ">"),
            ("cmd",  "Sylvain"),
            ("data", "Hello Sylvain. Latest Message is 52260, Last listed is 0"),
            ("data", "Please enter your Home BBS using the Home command."),
            ("data", "de N9SEO>"),
            ("cmd",  "QTH Montreal, QC"),
            ("data", "QTH is Montreal, QC"),
            ("data", "de N9SEO>"),
            ("cmd",  "Home VA2OPS.#MTL.QC.CAN.NOAM"),
            ("data", "HomeBBS is VA2OPS.#MTL.QC.CAN.NOAM"),
            ("data", "de N9SEO>"),
            ("cmd",  "ll 5"),
            ("data", "52257 21-Apr B$ 1689 EQUAKE @WW CX2SA (M5.6) off the east coast of Honshu, Japan 40.2 143.5"),
            ("data", "52256 21-Apr B$ 1358 JNOS2 @WW I0OJJ decode jnos-2.0q1-dev.txz"),
            ("data", "52255 21-Apr B$ 5077 NEWS @WW LU9DCE ONALLBANDS 21-APR"),
            ("data", "52253 21-Apr B$ 689 JNOS2 @WW KQ6UP RE:7+ jnos-2.0q1-dev.txz 5/5 <+7Plus"),
            ("data", "52252 21-Apr B$ 4645 AGCW @WW DJ6UX EuCW Snakes & Ladders 2026/03"),
            ("data", "de N9SEO>"),
        ]

        for kind, line in RECEIVED:
            if kind == "cmd":
                new_state, msg = simulate_command(line, state, node_call)
                print(f"  CMD   {line!r:40s}  state={state} {msg}")
                state = new_state
                continue

            # Data line processing
            prefix = ""

            # Re-detection
            if state != IDLE:
                matched, nc = detect_banner(line)
                if matched and nc and nc != node_call:
                    state = CONNECTED
                    node_call = nc
                    prefix = f"RE-DETECT node={nc!r}"
                    print(f"  DATA  {line!r:40s}  {prefix}")
                    continue

            if state in (IDLE, CONNECTED):
                matched, nc = detect_banner(line)
                if matched:
                    state = CONNECTED
                    if nc and nc != node_call:
                        node_call = nc
                    prefix = f"BANNER node={node_call!r}"
                if state == CONNECTED and detect_prompt(line):
                    pm = RX_PROMPT.search(line)
                    if not node_call and pm:
                        node_call = pm.group(1)
                    prefix = f"PROMPT → AtPrompt node={node_call!r}"
                    state = AT_PROMPT

            elif state == PARSING_LIST:
                if detect_prompt(line):
                    prefix = "END-LIST → AtPrompt"
                    state = AT_PROMPT
                else:
                    b = parse_list_line(line)
                    if b:
                        stored.append(b)
                        prefix = f"BULLETIN #{b['msg_id']} {b['type']} {b['from_call']}"
                    else:
                        prefix = f"NO-MATCH ← *** BUG ***"

            if not prefix:
                prefix = f"(state={state}, ignored)"

            print(f"  DATA  {line!r:40s}  {prefix}")

        print(f"\n  ── Result ──")
        print(f"  State    : {state}")
        print(f"  Node     : {node_call!r}")
        print(f"  Stored   : {len(stored)} bulletins")
        for b in stored:
            print(f"    #{b['msg_id']}  {b['date']}  {b['type']}  {b['category']}  {b['from_call']}  {b['title'][:45]}")

        print("\n\n=== N3MEL-2 (node-hop via local node) ===")
        simulate(N3MEL_SESSION)
