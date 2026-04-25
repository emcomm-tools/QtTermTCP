#!/bin/bash
# Build bbs-directory.json from TPRFN hub PDF profiles
#
# Usage: ./build-bbs-directory.sh
#
# Run this script whenever TPRFN hub stations are added or updated.
# Output: bbs-directory.json in the same directory — commit and rebuild ISO.
#
# To add a new station: add its callsign to the CALLSIGNS array below.
# If a station has a complex schedule or non-standard PDF format, add it
# as a manual entry (see K7EK example below).
#
# Source: TPRFN hub profiles at https://www.tprfn.net/tprfn-hub-profiles
# PDFs:   https://tprfn.k1ajd.net/hub-{CALLSIGN}.pdf

BASE_URL="https://tprfn.k1ajd.net"
OUT="/home/va2ops/QtTermTCP/bbs-directory.json"
TMP=$(mktemp -d)
UA="Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 Chrome/120.0.0.0 Safari/537.36"

CALLSIGNS=(
    AA5AF
    K1AJD K1AJD-7
    K5DAT K5DAT-1
    K7EK
    KA0PND KA0PND-1
    KA3VSP KA3VSP-1
    KD4WLE
    KD6MTU KD6MTU-1
    KK4DIV KK4DIV-1
    KN4LQN KN4LQN-2
    KP3FT
    N3HYM
    N3MEL N3MEL-2
    N4SD N4SD-1
    N4SFL N4SFL-1
    N5MDT N5MDT-4
    N9SEO N9SEO-1
    VA2OPS
    W1DTX W1DTX-4
    W7BMH W7BMH-1
    WC9P
    WW6Q WW6Q-1
)

freq_to_band() {
    local hz=$1
    if   [ "$hz" -ge 1800000  ] && [ "$hz" -le 1999999  ]; then echo "160m"
    elif [ "$hz" -ge 3500000  ] && [ "$hz" -le 4000000  ]; then echo "80m"
    elif [ "$hz" -ge 5000000  ] && [ "$hz" -le 5450000  ]; then echo "60m"
    elif [ "$hz" -ge 7000000  ] && [ "$hz" -le 7300000  ]; then echo "40m"
    elif [ "$hz" -ge 10100000 ] && [ "$hz" -le 10150000 ]; then echo "30m"
    elif [ "$hz" -ge 14000000 ] && [ "$hz" -le 14350000 ]; then echo "20m"
    elif [ "$hz" -ge 18068000 ] && [ "$hz" -le 18168000 ]; then echo "17m"
    elif [ "$hz" -ge 21000000 ] && [ "$hz" -le 21450000 ]; then echo "15m"
    elif [ "$hz" -ge 24890000 ] && [ "$hz" -le 24990000 ]; then echo "12m"
    elif [ "$hz" -ge 28000000 ] && [ "$hz" -le 29700000 ]; then echo "10m"
    elif [ "$hz" -ge 50000000 ] && [ "$hz" -le 54000000 ]; then echo "6m"
    elif [ "$hz" -ge 144000000 ] && [ "$hz" -le 148000000 ]; then echo "2m"
    else echo ""
    fi
}

# Read a field that follows a label line with a blank line between (PDF column layout)
# Usage: read_field "label" file
read_field() {
    local label=$1
    local file=$2
    # Try: value on same line after label
    local val=$(grep "^${label}" "$file" | sed "s/^${label}[[:space:]]*//" | tr -d '\r' | xargs)
    # If empty, try: blank line then value (PDF two-column layout)
    if [ -z "$val" ]; then
        val=$(grep -A2 "^${label}$" "$file" | tail -1 | tr -d '\r' | xargs)
    fi
    echo "$val"
}

parse_pdf() {
    local call=$1
    local txt=$2

    # Location: first content line is "CALLSIGN City, State"
    local location=$(head -6 "$txt" | grep -v "^TPRFN\|^The Packet\|^$\|Generated" | \
        grep -v "^Hub Station\|^Station Operator" | head -1 | \
        sed "s/^${call}[[:space:]]*//" | tr -d '\r' | xargs)
    echo "$location" | grep -qi "location unknown" && location=""

    local sysop=$(read_field "Sysop:" "$txt")
    local power=$(read_field "Power:" "$txt")
    echo "$power" | grep -qE '^[0-9]+$' && power="${power}W"
    local antenna=$(read_field "Antenna:" "$txt")
    local grid=$(grep -oE '[A-R]{2}[0-9]{2}[a-x]{2}' "$txt" | head -1)
    # Fallback grid table — manually verified
    if [ -z "$grid" ]; then
        case "$call" in
            K1AJD-7)  grid="EM83xl" ;;
            K5DAT-1)  grid="EN54qs" ;;
            KA0PND-1) grid="EN42lh" ;;
            KA3VSP-1) grid="FM29ep" ;;
            KD6MTU-1) grid="DN10qj" ;;
            KK4DIV-1) grid="EM70ef" ;;
            KN4LQN-2) grid="FM17ei" ;;
            N3HYM)    grid="FM19fk" ;;
            N3MEL-2)  grid="FM29dx" ;;
            N4SFL-1)  grid="EL96wi" ;;
            N5MDT-4)  grid="EM20ej" ;;
            N9SEO-1)  grid="EM36tg" ;;
            W1DTX-4)  grid="FN54vm" ;;
            W7BMH-1)  grid="CN84ka" ;;
            WW6Q-1)   grid="EN71lo" ;;
        esac
    fi

    local hours="00-00"
    grep -qi "24/7\|24h\|always" "$txt" && hours="00-00"

    # Parse frequencies from bullet lines between "Operating Frequencies" and next section
    local bands=""
    local first=1
    local seen_freqs=""

    while IFS= read -r line; do
        line=$(echo "$line" | sed 's/^[•\*\-][[:space:]]*//' | tr -d '\r' | xargs)
        [ -z "$line" ] && continue

        local num=$(echo "$line" | grep -oE '^[0-9]+\.?[0-9]*')
        [ -z "$num" ] && continue

        local hz=0
        local int_part=$(echo "$num" | cut -d'.' -f1)

        if [ "$int_part" -ge 100 ] && [ "$int_part" -le 30000 ]; then
            hz=$(echo "$num * 1000" | bc | cut -d'.' -f1)
        elif [ "$int_part" -ge 1 ] && [ "$int_part" -le 30 ]; then
            hz=$(echo "$num * 1000000" | bc | cut -d'.' -f1)
        elif [ "$int_part" -gt 30000 ] && [ "$int_part" -le 300000 ]; then
            # PDF formatting artifact: e.g. "71032 kHz" should be "7103.2 kHz"
            # Try dividing by 10
            hz=$(echo "$int_part / 10 * 1000" | bc)
        else
            continue
        fi

        [ "$hz" -lt 1800000 ] && continue
        [ "$hz" -gt 30000000 ] && continue

        echo "$seen_freqs" | grep -q "^${hz}$" && continue
        seen_freqs="${seen_freqs}
${hz}"

        local band=$(freq_to_band $hz)
        [ -z "$band" ] && continue

        [ $first -eq 0 ] && bands+=","
        bands+="{\"band\":\"$band\",\"frequency\":$hz,\"mode\":\"VARA HF\",\"bandwidth\":[500],\"hours\":\"$hours\"}"
        first=0
    done < <(sed -n '/Operating Frequencies/,/Station Equipment/p' "$txt" | grep -E '^[•\*\-]|^[0-9]')

    [ -z "$bands" ] && return

    sysop=$(echo "$sysop"     | sed 's/"/\\"/g')
    location=$(echo "$location" | sed 's/"/\\"/g')
    antenna=$(echo "$antenna"  | sed 's/"/\\"/g')
    power=$(echo "$power"     | sed 's/"/\\"/g')

    cat <<EOF
{
  "callsign": "$call",
  "sysop": "$sysop",
  "network": "TPRFN",
  "location": "$location",
  "grid": "$grid",
  "power": "$power",
  "antenna": "$antenna",
  "notes": "",
  "bands": [$bands]
}
EOF
}

echo "Fetching TPRFN hub PDFs..."
echo ""

ENTRIES=""
FIRST=1

add_entry() {
    local entry=$1
    [ $FIRST -eq 0 ] && ENTRIES+=","$'\n'
    ENTRIES+="$entry"
    FIRST=0
}

for CALL in "${CALLSIGNS[@]}"; do
    # K7EK: frequencies in notes with time schedule + explicit 500/2300/2750 BW
    if [ "$CALL" = "K7EK" ]; then
        echo "  K7EK ... manual (complex schedule)"
        add_entry '{
  "callsign": "K7EK",
  "sysop": "Gary",
  "network": "TPRFN",
  "location": "Radcliff, Kentucky",
  "grid": "EM77at",
  "power": "75W",
  "antenna": "Hustler 6BTV vertical with elevated radials",
  "notes": "Scanning schedule: 0000-1259z 80m, 1300-1859z 40m, 1900-2359z 30m",
  "bands": [
    {"band":"80m","frequency":3596000,"mode":"VARA HF","bandwidth":[500,2300,2750],"hours":"00-12"},
    {"band":"40m","frequency":7103200,"mode":"VARA HF","bandwidth":[500,2300,2750],"hours":"13-18"},
    {"band":"30m","frequency":10143000,"mode":"VARA HF","bandwidth":[500,2300,2750],"hours":"19-00"}
  ]
}'
        continue
    fi

    URL="${BASE_URL}/hub-${CALL}.pdf"
    PDF="$TMP/${CALL}.pdf"
    TXT="$TMP/${CALL}.txt"

    echo -n "  $CALL ... "
    HTTP=$(curl -s -A "$UA" -o "$PDF" -w "%{http_code}" "$URL")
    if [ "$HTTP" != "200" ]; then
        echo "no PDF ($HTTP)"
        continue
    fi

    pdftotext "$PDF" "$TXT" 2>/dev/null
    if [ ! -s "$TXT" ]; then
        echo "PDF empty"
        continue
    fi

    ENTRY=$(parse_pdf "$CALL" "$TXT")
    if [ -z "$ENTRY" ]; then
        echo "no frequencies found"
        continue
    fi

    echo "OK"
    add_entry "$ENTRY"
done

echo ""
echo "Writing $OUT ..."
printf '{"version":"1.0","stations":[\n%s\n]}\n' "$ENTRIES" > "$OUT"

rm -rf "$TMP"
COUNT=$(grep -c '"callsign"' "$OUT")
echo "Done — $COUNT stations written."
