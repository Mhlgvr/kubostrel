#!/bin/bash
# Builds Kubostrel and installs it on the iPhones connected to this Mac, or with --mac starts it on the Mac,
# or with --testflight uploads it to App Store Connect for TestFlight.
# Run it in Terminal (or double-click it in Finder). INSTALL.md explains the setup.
#
#   bash install_iphone.command                  build, then install on every iPhone on a cable or Wi-Fi
#   bash install_iphone.command --install-only   install the last build again without rebuilding
#   bash install_iphone.command --mac            build and start the game on this Mac (play_mac.command)
#   bash install_iphone.command --testflight     build the App Store version and upload it to TestFlight
#   bash install_iphone.command --upload-only    upload the last TestFlight build again without rebuilding
#   bash install_iphone.command --ipa            pack the last build into Saved/Kubostrel.ipa for friends
#
# Optional environment variables:
#   UE_ROOT=/path/to/UE_5.8          Unreal Engine folder, if the Epic Games Launcher put it elsewhere
#   XCODE_APP=/Applications/Xcode.app   Xcode to build with (by default an installed Xcode 26 is picked)
#   TEAM_ID=ABCDE12345               Apple team to sign with, when Xcode knows several
#   BUNDLE_PREFIX=com.yourname       app ID prefix, if com.mhlgvr is already taken by another Apple ID

set -o pipefail

INSTALL_ONLY=0
MAC_MODE=0
STORE_MODE=0
UPLOAD_ONLY=0
IPA_MODE=0
for arg in "$@"; do
	case "$arg" in
		--install-only) INSTALL_ONLY=1 ;;
		--mac) MAC_MODE=1 ;;
		--testflight) STORE_MODE=1 ;;
		--upload-only) STORE_MODE=1; UPLOAD_ONLY=1 ;;
		--ipa) IPA_MODE=1 ;;
		*) echo "Неизвестный параметр: $arg"; exit 2 ;;
	esac
done
if [ $((INSTALL_ONLY + MAC_MODE + STORE_MODE + IPA_MODE)) -gt 1 ]; then
	echo "Укажи что-то одно: --install-only, --mac, --testflight, --upload-only или --ipa."
	exit 2
fi
# How to run this script again the same way, for the hints.
RERUN="bash install_iphone.command"
if [ "$UPLOAD_ONLY" = 1 ]; then
	RERUN="$RERUN --upload-only"
elif [ "$STORE_MODE" = 1 ]; then
	RERUN="$RERUN --testflight"
fi

cd "$(dirname "$0")" || exit 1
PROJECT_DIR="$(pwd -P)"
PROJECT="$PROJECT_DIR/Kubostrel.uproject"
ENGINE_INI="$PROJECT_DIR/Config/DefaultEngine.ini"
HELPER="$PROJECT_DIR/Tools/ks_install.py"
LOG_DIR="$PROJECT_DIR/Saved/InstallLogs"
PACKAGE_DIR="$PROJECT_DIR/Saved/Packages"
STORE_DIR="$PROJECT_DIR/Saved/TestFlight"
ARCHIVE="$STORE_DIR/Kubostrel.xcarchive"
# The paid team a TestFlight build was signed with, for Macs where Xcode does not say which team is paid.
STORE_TEAM_FILE="$STORE_DIR/team.txt"
XCODE_SECTION="/Script/MacTargetPlatform.XcodeProjectSettings"
IOS_SECTION="/Script/IOSRuntimeSettings.IOSRuntimeSettings"
mkdir -p "$LOG_DIR" || exit 1

if [ -t 1 ]; then
	BOLD=$'\033[1m'; RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; RESET=$'\033[0m'
else
	BOLD=""; RED=""; GREEN=""; YELLOW=""; RESET=""
fi

step() { echo; echo "${BOLD}== $* ==${RESET}"; }
note() { echo "   $*"; }
good() { echo "${GREEN}   ✓ $*${RESET}"; }
warn() { echo "${YELLOW}   ! $*${RESET}"; }
fail() {
	{
		echo
		echo "${RED}${BOLD}Не получилось:${RESET}${RED} $*${RESET}"
		echo "Журналы сборки лежат в папке: $LOG_DIR"
		echo "Что делать при ошибках, написано в INSTALL.md (раздел «Если что-то пошло не так»)."
	} >&2
	exit 1
}

# Prints the error lines of a log, indented, so the reason is visible without opening the file.
show_errors() {
	grep -E "error:|Error:|ERROR:|❌" "$1" 2>/dev/null | sed 's/^[[:space:]]*/      /' | tail -n "${2:-12}"
}

# Waits for Enter in the Terminal window; stops the script if nobody can press it.
wait_enter() {
	if [ -r /dev/tty ] && [ -t 1 ]; then
		read -r -p "   $1 " _ </dev/tty
	else
		fail "$2"
	fi
}

[ "$(uname -s)" = "Darwin" ] || fail "этот скрипт работает только на Mac."
[ -f "$PROJECT" ] || fail "не нашёл Kubostrel.uproject рядом со скриптом."
if [ "$UPLOAD_ONLY" = 1 ] && [ ! -f "$ARCHIVE/Info.plist" ]; then
	fail "ещё нет сборки для TestFlight. Сначала запусти: bash install_iphone.command --testflight"
fi

if [ "$MAC_MODE" = 1 ]; then
	STEPS=3
	echo "${BOLD}Кубострел: запуск на Mac${RESET}"
	echo "Первый запуск долгий: Unreal компилирует игру и готовит шейдеры. Mac не уснёт, пока скрипт работает."
elif [ "$IPA_MODE" = 1 ]; then
	STEPS=3
	echo "${BOLD}Кубострел: файл игры для друзей${RESET}"
elif [ "$STORE_MODE" = 1 ]; then
	STEPS=5
	echo "${BOLD}Кубострел: сборка и загрузка в TestFlight${RESET}"
	[ "$UPLOAD_ONLY" = 1 ] || echo "Версия для App Store собирается заново, без отладки, это долго. Mac не уснёт, пока скрипт работает."
else
	STEPS=5
	echo "${BOLD}Кубострел: сборка и установка на iPhone${RESET}"
	echo "Первая сборка долгая: Unreal готовит шейдеры для iPhone. Mac не уснёт, пока скрипт работает."
fi
command -v caffeinate >/dev/null 2>&1 && { caffeinate -i -w $$ >/dev/null 2>&1 & }

# ---------------------------------------------------------------------------------------------
step "1/$STEPS. Проверяю Xcode"

xcode_version() {
	/usr/libexec/PlistBuddy -c "Print :CFBundleShortVersionString" "$1/Contents/Info.plist" 2>/dev/null
}

# Unreal Engine 5.8 is tested with Xcode 26.1; newer 26.x mostly works, Xcode 27 is not supported yet.
xcode_rank() {
	case "$1" in
		26.1|26.1.*) echo 4 ;;
		26.0|26.0.*|26.2|26.2.*|26.3|26.3.*) echo 3 ;;
		26.*) echo 2 ;;
		*) echo 1 ;;
	esac
}

list_xcodes() {
	local selected app
	selected="$(xcode-select -p 2>/dev/null)"
	case "$selected" in
		*.app/Contents/Developer) [ -d "$selected" ] && echo "${selected%/Contents/Developer}" ;;
	esac
	for app in /Applications/Xcode*.app "$HOME"/Applications/Xcode*.app; do
		[ -d "$app/Contents/Developer" ] && echo "$app"
	done
}

XCODE=""
XCODE_VERSION=""
if [ -n "$XCODE_APP" ]; then
	XCODE="${XCODE_APP%/}"
	XCODE_VERSION="$(xcode_version "$XCODE")"
else
	best=0
	while IFS= read -r app; do
		[ -n "$app" ] || continue
		version="$(xcode_version "$app")"
		[ -n "$version" ] || continue
		rank="$(xcode_rank "$version")"
		if [ "$rank" -gt "$best" ]; then
			best="$rank"; XCODE="$app"; XCODE_VERSION="$version"
		fi
	done <<EOF
$(list_xcodes)
EOF
fi
[ -n "$XCODE" ] && [ -d "$XCODE/Contents/Developer" ] || fail "не нашёл Xcode. Установи Xcode 26.1.1 (INSTALL.md, шаг 1)."
export DEVELOPER_DIR="$XCODE/Contents/Developer"
note "Xcode $XCODE_VERSION: $XCODE"
case "$XCODE_VERSION" in
	26.*) ;;
	*) warn "Unreal Engine 5.8 рассчитан на Xcode 26 (лучше всего 26.1.1). С Xcode $XCODE_VERSION сборка может не пройти." ;;
esac

if ! xcodebuild -version >"$LOG_DIR/xcode.log" 2>&1; then
	sed 's/^/      /' "$LOG_DIR/xcode.log" | head -n 5
	fail "Xcode ещё не готов. Открой его один раз, прими лицензию и дождись, пока он установит компоненты."
fi
METAL_SDK=macosx
if [ "$MAC_MODE" = 0 ]; then
	METAL_SDK=iphoneos
	xcrun --sdk iphoneos --show-sdk-path >/dev/null 2>&1 || fail "в Xcode нет iOS SDK. Открой Xcode → Settings → Components и установи iOS."
fi
if [ "$IPA_MODE" = 0 ] && ! xcrun --sdk "$METAL_SDK" metal --version >/dev/null 2>&1; then
	note "Скачиваю Metal Toolchain для Xcode, он нужен для шейдеров..."
	xcodebuild -downloadComponent MetalToolchain || fail "не скачался Metal Toolchain. Поставь его вручную: Xcode → Settings → Components → Metal Toolchain."
fi
good "Xcode готов"

# ---------------------------------------------------------------------------------------------
step "2/$STEPS. Ищу Unreal Engine 5.8"

find_ue() {
	if [ -n "$UE_ROOT" ]; then
		echo "${UE_ROOT%/}"
		return
	fi
	local launcher="$HOME/Library/Application Support/Epic/UnrealEngineLauncher/LauncherInstalled.dat"
	{
		[ -f "$launcher" ] && sed -n 's/.*"InstallLocation"[[:space:]]*:[[:space:]]*"\([^"]*UE_5\.[0-9][0-9]*\)".*/\1/p' "$launcher"
		ls -d "/Users/Shared/Epic Games"/UE_5.* 2>/dev/null
	} | while IFS= read -r dir; do
		[ -x "$dir/Engine/Build/BatchFiles/RunUAT.sh" ] || continue
		minor="${dir##*UE_5.}"
		case "$minor" in ''|*[!0-9]*) continue ;; esac
		[ "$minor" -ge 8 ] && printf '%s\t%s\n' "$minor" "$dir"
	done | sort -n | awk -F'\t' '$1 == 8 { print $2; found = 1; exit } { last = $2 } END { if (!found && last != "") print last }'
}

UE="$(find_ue)"
[ -n "$UE" ] && [ -x "$UE/Engine/Build/BatchFiles/RunUAT.sh" ] || fail "не нашёл Unreal Engine 5.8. Установи его через Epic Games Launcher (INSTALL.md, шаг 2)."
UE_EDITOR="$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
[ -x "$UE_EDITOR" ] || fail "в $UE нет редактора Unreal. Переустанови Unreal Engine 5.8 в Epic Games Launcher."
note "Unreal Engine: $UE"
case "$UE" in
	*UE_5.8) ;;
	*) warn "Проект сделан для версии 5.8, а нашлась другая. Скорее всего сработает, но лучше поставить 5.8." ;;
esac
if [ "$MAC_MODE" = 0 ] && [ ! -d "$UE/Engine/Binaries/IOS" ] && [ ! -d "$UE/Engine/Intermediate/Build/IOS" ]; then
	warn "Похоже, у Unreal Engine не установлена поддержка iOS. Epic Games Launcher → Unreal Engine → Библиотека → ▾ у версии 5.8 → Параметры → отметь iOS → Применить."
fi

PY=""
for candidate in "$UE/Engine/Binaries/ThirdParty/Python3/Mac/bin/python3" "$DEVELOPER_DIR/usr/bin/python3" /usr/bin/python3; do
	if [ -x "$candidate" ] && "$candidate" -c "import json, plistlib" >/dev/null 2>&1; then
		PY="$candidate"
		break
	fi
done
[ -n "$PY" ] || fail "не нашёл Python 3 (он идёт вместе с Unreal Engine и Xcode). Переустанови Unreal Engine 5.8."

free_gb="$(df -g "$PROJECT_DIR" 2>/dev/null | awk 'NR == 2 { print $4 }')"
if [ -n "$free_gb" ] && [ "$free_gb" -lt 20 ] 2>/dev/null; then
	warn "На диске свободно $free_gb ГБ, а первой сборке нужно около 20 ГБ."
fi
good "Unreal Engine найден"

# Compiles the game code for the Unreal editor and creates the materials. The iPhone build
# needs both, and on the Mac the editor runs the game itself.
build_editor_and_materials() {
	local name asset materials_ok=1
	note "Компилирую код игры для редактора Unreal..."
	"$UE/Engine/Build/BatchFiles/Mac/Build.sh" KubostrelEditor Mac Development -project="$PROJECT" -waitmutex </dev/null 2>&1 \
		| tee "$LOG_DIR/2-editor.log" | { grep --line-buffered -E '^\[[0-9]+/[0-9]+\]|error:|Result:|Total execution time' || true; }
	[ "${PIPESTATUS[0]}" = 0 ] || { show_errors "$LOG_DIR/2-editor.log"; fail "код игры не скомпилировался (журнал: Saved/InstallLogs/2-editor.log)."; }

	for name in M_KS_Surface M_KS_Neon M_KS_Glow M_KS_Character; do
		asset="$PROJECT_DIR/Content/KS/Materials/$name.uasset"
		[ -f "$asset" ] && [ "$asset" -nt "$PROJECT_DIR/Content/Python/ks_content.py" ] || materials_ok=0
	done
	[ "$materials_ok" = 1 ] && return 0
	note "Создаю материалы (неон, сетка на стенах, свечение роботов)..."
	"$UE_EDITOR" "$PROJECT" -run=pythonscript -script="$PROJECT_DIR/Content/Python/ks_content.py" \
		-unattended -nosplash -nullrhi -nop4 -stdout -FullStdOutLogOutput </dev/null 2>&1 \
		| tee "$LOG_DIR/3-materials.log" | { grep --line-buffered -E '\[Kubostrel\]' || true; }
	for name in M_KS_Surface M_KS_Neon M_KS_Glow M_KS_Character; do
		if [ ! -f "$PROJECT_DIR/Content/KS/Materials/$name.uasset" ]; then
			warn "Материалы не создались (журнал: Saved/InstallLogs/3-materials.log). Игра запустится, но будет серой."
			return 0
		fi
	done
	# Up-to-date materials are not saved again; a fresh time stamp lets the next run skip this step.
	touch "$PROJECT_DIR/Content/KS/Materials/"M_KS_*.uasset
}

# ---------------------------------------------------------------------------------------------
if [ "$MAC_MODE" = 1 ]; then
	step "3/3. Собираю и запускаю игру на Mac"
	build_editor_and_materials
	good "Игра собрана"
	note "Запускаю Кубострел в отдельном окне. Первый запуск дольше: Unreal готовит шейдеры для Mac,"
	note "и окно может несколько минут оставаться чёрным. Не закрывай Терминал, пока играешь."
	note "Управление: WASD и мышь, пробел — прыжок, 1–4 — оружие, R — перезарядка, Tab — счёт, Esc — меню."
	if ! "$UE_EDITOR" "$PROJECT" -game -windowed -ResX=1280 -ResY=720 </dev/null >"$LOG_DIR/mac-game.log" 2>&1; then
		show_errors "$LOG_DIR/mac-game.log"
		fail "игра закрылась с ошибкой (журнал: Saved/InstallLogs/mac-game.log)."
	fi
	echo
	echo "Игра закрыта. Запустить снова: bash play_mac.command"
	exit 0
fi

# Leaves in APP the app Unreal made, taking it out of the .ipa if it made one.
open_app() {  # .app or .ipa
	APP="$1"
	case "$APP" in
		*.ipa)
			rm -rf "$LOG_DIR/ipa" && mkdir -p "$LOG_DIR/ipa"
			ditto -x -k "$APP" "$LOG_DIR/ipa" || fail "не смог распаковать $APP."
			APP="$(ls -d "$LOG_DIR/ipa/Payload/"*.app 2>/dev/null | head -n 1)"
			[ -n "$APP" ] || fail "в архиве нет приложения."
			;;
	esac
}

# The packaged app carries the cooked game data; an app without it opens to a black screen.
has_game_data() { [ -n "$(find "$1" \( -name '*.utoc' -o -name '*.pak' \) 2>/dev/null | head -n 1)" ]; }

# ---------------------------------------------------------------------------------------------
# Friends without this Mac install the game with their own free Apple ID (INSTALL.md, «Друзья ставят
# сами»), and the sideloading tool signs it for them. The last build is packed as it is, so everyone
# plays the same version. The signing profile is left out: it lists the UDIDs of the iPhones signed up.
if [ "$IPA_MODE" = 1 ]; then
	step "3/3. Упаковываю игру в файл для друзей"
	found="$("$PY" "$HELPER" find-app "$PROJECT_DIR/Binaries/IOS" "$PACKAGE_DIR")"
	[ -n "$found" ] || fail "ещё нет собранной игры. Сначала поставь её на свой iPhone: bash install_iphone.command"
	open_app "$found"
	note "Приложение: $APP"
	has_game_data "$APP" || fail "в приложении нет данных игры (файлов .pak и .utoc). Собери игру заново: bash install_iphone.command"
	IPA="$PROJECT_DIR/Saved/Kubostrel.ipa"
	rm -rf "$LOG_DIR/ipa-pack" "$IPA"
	mkdir -p "$LOG_DIR/ipa-pack/Payload" || exit 1
	cp -R "$APP" "$LOG_DIR/ipa-pack/Payload/" || fail "не смог скопировать приложение."
	rm -f "$LOG_DIR/ipa-pack/Payload/$(basename "$APP")/embedded.mobileprovision"
	(cd "$LOG_DIR/ipa-pack" && zip -q -r -y "$IPA" Payload) || fail "не смог упаковать игру в $IPA."
	rm -rf "$LOG_DIR/ipa-pack"
	good "Файл готов: Saved/Kubostrel.ipa ($(du -h "$IPA" | cut -f1))"
	echo
	echo "Отправь его друзьям, например в Telegram. Как поставить его на iPhone, написано в INSTALL.md,"
	echo "раздел «Друзья ставят сами». Когда обновишь игру, сделай и отправь файл заново: играть вместе"
	echo "можно только с одной версией."
	open -R "$IPA" >/dev/null 2>&1
	exit 0
fi

# ---------------------------------------------------------------------------------------------
if [ "$STORE_MODE" = 1 ]; then
	step "3/5. Подпись: платная команда Apple"
else
	step "3/5. Подпись: Apple ID и iPhone"
fi

ini_get() { "$PY" "$HELPER" get-ini "$ENGINE_INI" "$1" "$2" | tr -d '\r'; }
ini_set() { "$PY" "$HELPER" set-ini "$ENGINE_INI" "$1" "$2" "$3" || fail "не смог записать $2 в Config/DefaultEngine.ini."; }

list_teams() { "$PY" "$HELPER" teams "$STORE_TEAM_FILE" 2>>"$LOG_DIR/teams.log"; }
# 0 for a team of the paid Apple Developer Program, 1 for a free Personal Team, ? if not known.
team_kind() { printf '%s\n' "$TEAMS" | awk -F'\t' -v t="$1" '$1 == t { print $3; exit }'; }

# Only a team of the paid Apple Developer Program can upload to App Store Connect. Xcode 26 does not
# say which team is paid until it has made a signing profile for it, so the first time the script
# asks for the Team ID.
choose_store_team() {
	local paid typed
	if [ -n "$TEAM_ID" ]; then
		TEAM="$TEAM_ID"
	else
		paid="$(printf '%s\n' "$TEAMS" | awk -F'\t' '$3 == "0" { print $1 }')"
		if [ -n "$INI_TEAM" ] && printf '%s\n' "$paid" | grep -qx "$INI_TEAM"; then
			TEAM="$INI_TEAM"
		else
			TEAM="$(printf '%s\n' "$paid" | head -n 1)"
		fi
	fi
	[ -n "$TEAM" ] && [ "$(team_kind "$TEAM")" != 1 ] && return 0
	[ -z "$TEAM_ID" ] || fail "$TEAM_ID — бесплатная команда (Personal Team), с ней в TestFlight не загрузить. Нужен Team ID платной команды (INSTALL.md, раздел «TestFlight»)."
	echo
	note "Для TestFlight нужна платная команда Apple Developer Program, а Xcode её пока не показывает."
	note "1. Дождись письма от Apple, что членство в Apple Developer Program активно."
	note "2. Открой https://developer.apple.com/account, войди и скопируй Team ID из раздела Membership details."
	while :; do
		[ -r /dev/tty ] && [ -t 1 ] || fail "не знаю Team ID платной команды. Запусти так: TEAM_ID=XXXXXXXXXX $RERUN"
		read -r -p "   Вставь Team ID и нажми Enter (Ctrl+C — выйти): " typed </dev/tty
		typed="$(printf '%s' "$typed" | tr -d '[:space:]' | tr '[:lower:]' '[:upper:]')"
		if ! printf '%s' "$typed" | grep -Eq '^[A-Z0-9]{10}$'; then
			note "Team ID — это 10 латинских букв и цифр, например A1B2C3D4E5."
		elif [ "$(team_kind "$typed")" = 1 ]; then
			note "$typed — бесплатная команда (Personal Team). Нужен Team ID из раздела Membership details."
		else
			TEAM="$typed"
			return 0
		fi
	done
}

# Signing makes Xcode fetch a profile for the team, and how long it lasts tells a paid team from a
# free one. The team is kept for the next upload.
check_store_team() {
	TEAMS="$(list_teams)"
	if [ "$(team_kind "$TEAM")" = 1 ]; then
		rm -f "$STORE_TEAM_FILE"
		fail "$TEAM — бесплатная команда (Personal Team), с ней в TestFlight не загрузить. Запусти скрипт снова и введи Team ID из раздела Membership details на https://developer.apple.com/account."
	fi
	mkdir -p "$STORE_DIR" && printf '%s\n' "$TEAM" >"$STORE_TEAM_FILE"
}

TEAMS="$(list_teams)"
# Earlier versions of this script kept the team under ModernSigningTeam.
INI_TEAM="$(ini_get "$XCODE_SECTION" CodeSigningTeam)"
[ -n "$INI_TEAM" ] || INI_TEAM="$(ini_get "$XCODE_SECTION" ModernSigningTeam)"
if [ "$STORE_MODE" = 1 ]; then
	choose_store_team
else
	# Recent Xcode no longer lists the teams of a newly added Apple ID in its preferences, so on a new
	# Mac the team is known only once Xcode has made a development certificate for it.
	while [ -z "$TEAM_ID" ] && [ -z "$TEAMS" ] && [ -z "$INI_TEAM" ]; do
		echo
		note "Не нашёл Apple ID, которым подписать игру. Сделай в Xcode:"
		note "1. Settings → Apple Accounts (или Accounts) → +, выбери Apple ID и войди, если его там ещё нет."
		note "2. Выбери свой Apple ID → Manage Certificates… → + → Apple Development → Done."
		wait_enter "Потом нажми Enter (Ctrl+C — выйти)..." "в Xcode нет Apple ID или сертификата Apple Development (INSTALL.md, шаг 1)."
		TEAMS="$(list_teams)"
	done
	if [ -n "$TEAM_ID" ]; then
		TEAM="$TEAM_ID"
	elif [ -n "$INI_TEAM" ] && printf '%s\n' "$TEAMS" | cut -f1 | grep -qx "$INI_TEAM"; then
		TEAM="$INI_TEAM"
	elif [ -n "$TEAMS" ]; then
		# A paid team signs for a year, a free Personal Team for 7 days, so a paid one wins.
		TEAM="$(printf '%s\n' "$TEAMS" | awk -F'\t' '$3 == "0" { print $1; exit }')"
		[ -n "$TEAM" ] || TEAM="$(printf '%s\n' "$TEAMS" | head -n 1 | cut -f1)"
	else
		TEAM="$INI_TEAM"
	fi
fi
TEAM_NAME="$(printf '%s\n' "$TEAMS" | awk -F'\t' -v t="$TEAM" '$1 == t && $2 != "-" { print $2; exit }')"
note "Команда для подписи: ${TEAM_NAME:-$TEAM} ($TEAM)"
if [ -z "$TEAM_ID" ]; then
	# For TestFlight only the paid teams are a choice.
	choices="$(printf '%s\n' "$TEAMS" | awk -F'\t' -v store="$STORE_MODE" 'NF && (store == 0 || $3 == "0")' | grep -c .)"
	if [ "$choices" -gt 1 ]; then
		note "Xcode знает несколько команд. Другую можно выбрать так: TEAM_ID=XXXXXXXXXX $RERUN"
	fi
fi
if [ -n "$BUNDLE_PREFIX" ]; then
	printf '%s' "$BUNDLE_PREFIX" | LC_ALL=C grep -Eq '^[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)*$' \
		|| fail "в BUNDLE_PREFIX можно писать только латинские буквы, цифры, точки и дефисы, например com.ivanov."
	PREFIX="$BUNDLE_PREFIX"
else
	PREFIX="$(ini_get "$XCODE_SECTION" CodeSigningPrefix)"
	[ -n "$PREFIX" ] || PREFIX="$(ini_get "$XCODE_SECTION" ModernSigningPrefix)"
	PREFIX="${PREFIX:-com.mhlgvr}"
fi
# Unreal names the app <prefix>.<project name>.
BUNDLE_ID="$PREFIX.Kubostrel"

# UnrealBuildTool hands these to Xcode. Epic's documentation calls them bUseModernCodeSigning,
# ModernSigningPrefix and ModernSigningTeam, but Unreal does not read those names.
ini_update() { [ "$(ini_get "$1" "$2")" = "$3" ] || ini_set "$1" "$2" "$3"; }
ini_update "$XCODE_SECTION" bUseAutomaticCodeSigning True
ini_update "$XCODE_SECTION" CodeSigningPrefix "$PREFIX"
ini_update "$XCODE_SECTION" CodeSigningTeam "$TEAM"
ini_update "$IOS_SECTION" BundleIdentifier "$BUNDLE_ID"
ini_update "$IOS_SECTION" IOSTeamID "$TEAM"

list_devices() {
	rm -f "$LOG_DIR/devices.json"
	if ! xcrun devicectl list devices --json-output "$LOG_DIR/devices.json" >"$LOG_DIR/devicectl.log" 2>&1 </dev/null; then
		sed 's/^/      /' "$LOG_DIR/devicectl.log" | tail -n 5 >&2
		fail "Xcode не может работать с iPhone (devicectl). Открой Xcode один раз и дождись установки компонентов."
	fi
	"$PY" "$HELPER" devices "$LOG_DIR/devices.json"
}

# Waits until an iPhone is connected, trusts this Mac and has Developer Mode on.
# Leaves the list of connected devices in DEVICES.
DEVICES=""
wait_for_devices() {
	local ready id udid name os devmode pairing transport
	while :; do
		DEVICES="$(list_devices)" || exit 1
		if [ -z "$DEVICES" ]; then
			echo
			note "iPhone не найден. Подключи его к Mac кабелем, разблокируй и нажми «Доверять», если он спросит."
			note "Если этот iPhone уже подключался к Mac кабелем, хватит общей сети Wi-Fi: просто разблокируй его."
			wait_enter "Потом нажми Enter (Ctrl+C — выйти)..." "iPhone не подключён."
			continue
		fi
		ready=1
		while IFS=$'\t' read -r id udid name os devmode pairing transport; do
			[ -n "$id" ] || continue
			if [ "$pairing" != "paired" ] && [ "$pairing" != "-" ]; then
				ready=0
				note "$name ещё не доверяет этому Mac. Разблокируй iPhone и нажми «Доверять», потом введи код-пароль."
				xcrun devicectl manage pair --device "$id" >>"$LOG_DIR/devicectl.log" 2>&1 </dev/null
			elif [ "$devmode" = "disabled" ]; then
				ready=0
				note "На $name выключен режим разработчика. Включи его на iPhone:"
				note "Настройки → Конфиденциальность и безопасность → Режим разработчика (в самом низу) → включить."
				note "iPhone перезагрузится; после этого нажми «Включить» и введи код-пароль."
				note "Если пункта нет: открой Xcode → Window → Devices and Simulators с подключённым iPhone, он появится."
			fi
		done <<EOF
$DEVICES
EOF
		[ "$ready" = 1 ] && return 0
		wait_enter "Когда сделаешь, нажми Enter..." "iPhone ещё не готов к установке."
	done
}

show_devices() {
	local id udid name os devmode pairing transport
	while IFS=$'\t' read -r id udid name os devmode pairing transport; do
		[ -n "$id" ] && note "iPhone: $name, iOS $os ($( [ "$transport" = wired ] && echo "по кабелю" || echo "по Wi-Fi" ))"
	done <<EOF
$DEVICES
EOF
}

if [ "$STORE_MODE" = 0 ]; then
	wait_for_devices
	show_devices
elif [ "$UPLOAD_ONLY" = 0 ]; then
	# TestFlight needs no iPhone, but Xcode signs only for a team that has at least one registered.
	DEVICES="$(list_devices)" || exit 1
	if [ -n "$DEVICES" ]; then
		wait_for_devices
		show_devices
	else
		note "iPhone не подключён. Он нужен в первый раз: Xcode подписывает игру, только если в команде есть хотя бы один iPhone."
	fi
fi

# Xcode 26 came out before iOS 27, so it may not work with an iPhone that already has iOS 27.
# Signing and installing can then be done by a newer Xcode, while the game is still built with Xcode 26.
newer_ios_hint() {  # device name, iOS version
	local major="${2%%.*}" newest=0 app v
	case "$major" in ''|*[!0-9]*) return 1 ;; esac
	while IFS= read -r app; do
		[ -n "$app" ] || continue
		v="$(xcode_version "$app")"
		v="${v%%.*}"
		case "$v" in ''|*[!0-9]*) continue ;; esac
		[ "$v" -gt "$newest" ] && newest="$v"
	done <<EOF
$XCODE
$(list_xcodes)
EOF
	[ "$newest" -gt 0 ] && [ "$major" -gt "$newest" ] || return 1
	note "→ На $1 iOS $2, а самый новый Xcode на этом Mac вышел раньше этой версии iOS."
	note "  Поставь ещё Xcode $major из App Store, открой его один раз и дождись, пока он установит компоненты."
	note "  Потом запусти скрипт снова: подпишет и поставит игру новый Xcode, а соберёт её, как и раньше, Xcode $XCODE_VERSION."
	return 0
}

ID_TAKEN_PATTERN="cannot be registered to your development team|not available. Change your bundle identifier"
# Set by register_game: a taken app ID is reported in ID_TAKEN instead of with hints.
ID_FALLBACK=0
ID_TAKEN=0

# Builds a tiny placeholder app for the iPhone. With a free Apple ID this is how the phone gets
# registered with the team and how the signing profile for the game is created.
# Any installed Xcode can do it, so the others are tried if the main one fails.
register_device() {  # udid, name, bundle id, iOS version
	local log="$LOG_DIR/register-$1.log" xc label failed="" count
	local others
	others="$(list_xcodes | grep -v -x -F "$XCODE" | sort -u)"
	: >"$log"
	while IFS= read -r xc; do
		[ -n "$xc" ] || continue
		label="$(basename "$xc" .app)"
		if DEVELOPER_DIR="$xc/Contents/Developer" xcodebuild \
			-project "$PROJECT_DIR/Tools/DeviceSetup/KSDeviceSetup.xcodeproj" \
			-scheme KSDeviceSetup -configuration Debug \
			-destination "id=$1" \
			-derivedDataPath "$PROJECT_DIR/Intermediate/DeviceSetup/$label" \
			-allowProvisioningUpdates -allowProvisioningDeviceRegistration \
			DEVELOPMENT_TEAM="$TEAM" PRODUCT_BUNDLE_IDENTIFIER="$3" \
			build >"$LOG_DIR/register-$1-$label.log" 2>&1 </dev/null; then
			return 0
		fi
		cat "$LOG_DIR/register-$1-$label.log" >>"$log"
		failed="$failed$xc"$'\n'
	done <<EOF
$XCODE
$others
EOF
	if [ "$ID_FALLBACK" = 1 ] && grep -q -i -E "$ID_TAKEN_PATTERN" "$log"; then
		ID_TAKEN=1
		return 1
	fi
	echo
	note "Xcode не смог подписать приложение для $2:"
	count="$(printf '%s' "$failed" | grep -c .)"
	while IFS= read -r xc; do
		[ -n "$xc" ] || continue
		[ "$count" -gt 1 ] && note "Xcode $(xcode_version "$xc"):"
		show_errors "$LOG_DIR/register-$1-$(basename "$xc" .app).log" 6
	done <<EOF
$failed
EOF
	if grep -q -i -E "No Account|No Accounts|not signed in|session has expired|sign in" "$log"; then
		note "→ Войди в Apple ID в Xcode: Xcode → Settings → Apple Accounts (или Accounts) → +."
		[ "$STORE_MODE" = 1 ] && note "  Если он там уже есть, удали его кнопкой − и добавь снова: так Xcode увидит платную команду."
	fi
	if grep -q -i -E "$ID_TAKEN_PATTERN" "$log"; then
		note "→ ID $3 занят другим Apple ID или другой командой. Придумай любое слово латиницей и запусти так:"
		note "  BUNDLE_PREFIX=com.ivanov $RERUN"
	fi
	if grep -q -i -E "maximum number of registered|maximum number of (iPhone|iOS) devices" "$log"; then
		note "→ Бесплатный Apple ID пускает не больше 3 iPhone за неделю. Подожди, пока пройдёт неделя с тех пор,"
		note "  как подключался другой iPhone, или пусть друг поставит игру сам (INSTALL.md, «Друзья ставят сами»)."
	elif grep -q -i -E "maximum number of|reached the limit|limit of [0-9]" "$log"; then
		note "→ Бесплатный Apple ID разрешает 3 своих приложения на iPhone и 10 новых ID в неделю."
		note "  Удали с iPhone другие приложения, установленные из Xcode, или подожди несколько дней."
	fi
	if newer_ios_hint "$2" "$4"; then
		:
	elif grep -q -i -E "Unable to find a destination|Ineligible destination|is busy|preparing" "$log"; then
		note "→ Xcode ещё готовит iPhone. Открой Xcode → Window → Devices and Simulators, дождись, пока у iPhone пропадут предупреждения, и запусти скрипт снова."
	fi
	return 1
}

register_all() {  # bundle id
	local id udid name os devmode pairing transport
	while IFS=$'\t' read -r id udid name os devmode pairing transport; do
		[ -n "$id" ] || continue
		note "Регистрирую $name для подписи $1..."
		register_device "$udid" "$name" "$1" "$os" || return 1
	done <<EOF
$DEVICES
EOF
	return 0
}

# An app ID belongs to the team that registered it first, and Xcode registers the game's ID for the
# free Personal Team on the first install. So when the paid team (or a friend's Apple ID) finds the
# ID taken, another one is picked once, unless BUNDLE_PREFIX says which to use.
register_game() {
	local status
	ID_TAKEN=0
	ID_FALLBACK=1
	[ -z "$BUNDLE_PREFIX" ] || ID_FALLBACK=0
	case "$PREFIX" in *.games) ID_FALLBACK=0 ;; esac
	register_all "$BUNDLE_ID"
	status=$?
	ID_FALLBACK=0
	[ "$status" = 0 ] && return 0
	[ "$ID_TAKEN" = 1 ] || return 1
	if [ "$STORE_MODE" = 1 ]; then
		note "ID $BUNDLE_ID уже занят другой командой Apple (скорее всего, твоей бесплатной). Беру $PREFIX.games.Kubostrel."
	else
		note "ID $BUNDLE_ID уже занят другим Apple ID или другой командой. Беру $PREFIX.games.Kubostrel."
	fi
	PREFIX="$PREFIX.games"
	BUNDLE_ID="$PREFIX.Kubostrel"
	ini_update "$XCODE_SECTION" CodeSigningPrefix "$PREFIX"
	ini_update "$IOS_SECTION" BundleIdentifier "$BUNDLE_ID"
	register_all "$BUNDLE_ID"
}

if [ "$INSTALL_ONLY" = 0 ] && [ "$UPLOAD_ONLY" = 0 ] && [ -n "$DEVICES" ]; then
	note "Если Mac спросит пароль для «codesign», введи пароль от Mac и нажми «Всегда разрешать»."
	register_game || fail "iPhone не зарегистрирован для подписи (подробности выше)."
	good "iPhone зарегистрирован, подпись готова"
	[ "$STORE_MODE" = 0 ] || check_store_team
fi

# ---------------------------------------------------------------------------------------------
UAT_FILTER='\*{6,}|Packages Remain|Compiling [0-9]+ shaders|Shaders Left|[Ee]rror:|ERROR:|BUILD SUCCESSFUL|BUILD FAILED|AutomationTool exiting|ExitCode='

# Explains the usual reasons why the iPhone build fails.
package_hints() {  # log file
	if grep -q -i -E "Missing files required to build IOS" "$1"; then
		note "→ У Unreal Engine не скачана поддержка iOS. Epic Games Launcher → Unreal Engine → Библиотека → ▾ у версии 5.8 → Параметры → отметь iOS → Применить."
		note "  Дождись конца загрузки и запусти скрипт снова."
	elif grep -q -i -E "Platform IOS is not a valid platform" "$1"; then
		note "→ Unreal не видит iOS: проверь, что в Epic Games Launcher у версии 5.8 отмечена поддержка iOS, и что используется Xcode 26."
	fi
	if grep -q -E "errSecInternalComponent|User interaction is not allowed|unable to build chain" "$1"; then
		note "→ macOS не дал подписать приложение. Запусти скрипт снова и, когда Mac спросит пароль для «codesign», введи пароль от Mac и нажми «Всегда разрешать»."
	fi
	if grep -q -E "requires a development team" "$1"; then
		note "→ Unreal не передал Xcode команду подписи. Подпись в проекте Xcode, который он сделал:"
		grep -r -h -o -E "(CODE_SIGN_STYLE|DEVELOPMENT_TEAM|PRODUCT_BUNDLE_IDENTIFIER) = [^;]*" "$PROJECT_DIR/Intermediate/ProjectFilesIOS" 2>/dev/null \
			| sort -u | head -n 6 | sed 's/^/      /'
		note "  Открой Kubostrel.uproject, Edit → Project Settings → Platforms → Xcode Projects, включи Use Modern Code Signing,"
		note "  впиши $TEAM в Modern Signing Team, закрой Unreal и запусти скрипт снова."
	elif grep -q -i -E "has no devices|no devices from which" "$1"; then
		note "→ Xcode подписывает игру, только если в команде есть хотя бы один iPhone. Подключи iPhone кабелем и запусти скрипт снова."
	elif grep -q -E "No profiles for|No signing certificate" "$1"; then
		note "→ Проблема с подписью: открой Xcode → Settings → Apple Accounts, проверь, что Apple ID на месте, и запусти скрипт снова."
	fi
	if grep -q -i "No space left on device" "$1"; then
		note "→ Закончилось место на диске. Освободи хотя бы 20 ГБ."
	fi
}

# UnrealBuildTool often explains an early stop without the word "error", in the lines just before
# AutomationTool's "Took …s to run dotnet, ExitCode=N", so those lines are shown as they are.
show_tool_output() {  # log file
	awk '
		/to run [^ ]+, ExitCode=[1-9]/ {
			if (n > 0) print "   Последнее, что написал Unreal перед остановкой:"
			for (i = (n > 15 ? n - 15 : 0); i < n; i++) print "      " last[i % 15]
			exit
		}
		{ sub(/^[ \t]+/, ""); sub(/\r$/, "") }
		NF { last[n % 15] = $0; n++ }
	' "$1" 2>/dev/null
}

# TestFlight gets the Shipping build: no console or debug tools, and it runs faster.
CONFIG=Development
[ "$STORE_MODE" = 0 ] || CONFIG=Shipping
BUILD_MARK="$LOG_DIR/4-package.start"

# The packaged app stays in Binaries/IOS. There is no -archive: in UE 5.8 it runs "xcodebuild archive",
# which fails to copy the game data into the app. -SkipZenStore cooks to plain files: a cook into the
# Zen storage server (the default since UE 5.8) tries every network address of the Mac in turn and
# can hang on one of them.
package_game() {  # log file
	"$UE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
		-project="$PROJECT" -target=Kubostrel -platform=IOS -clientconfig="$CONFIG" \
		-nop4 -utf8output -unattended -nocompile -nocompileuat -nocompileeditor -skipbuildeditor \
		-unrealexe="$UE_EDITOR" -SkipZenStore \
		-build -cook -stage -pak -iostore -compressed -package </dev/null 2>&1 | tee "$1" | { grep --line-buffered -E "$UAT_FILTER" || true; }
	return "${PIPESTATUS[0]}"
}

if [ "$INSTALL_ONLY" = 0 ] && [ "$UPLOAD_ONLY" = 0 ]; then
	if [ "$STORE_MODE" = 1 ]; then
		step "4/5. Собираю версию для TestFlight"
		note "Пока идёт сборка, создай приложение в App Store Connect, если его ещё нет (INSTALL.md, раздел «TestFlight»)."
		note "Его Bundle ID: $BUNDLE_ID"
	else
		step "4/5. Собираю игру"
	fi

	build_editor_and_materials

	# The project's asset catalog (with the game icon) replaces the engine's one,
	# so everything else from the engine's catalog is copied next to the icon.
	ENGINE_ASSETS="$UE/Engine/Build/IOS/Resources/Assets.xcassets"
	PROJECT_ASSETS="$PROJECT_DIR/Build/IOS/Resources/Assets.xcassets"
	if [ -d "$ENGINE_ASSETS" ] && [ -d "$PROJECT_ASSETS" ]; then
		for item in "$ENGINE_ASSETS"/*; do
			[ -e "$item" ] || continue
			case "$(basename "$item")" in AppIcon.appiconset|Contents.json) continue ;; esac
			[ -e "$PROJECT_ASSETS/$(basename "$item")" ] || cp -R "$item" "$PROJECT_ASSETS/"
		done
	fi

	# A Zen cook leaves ue.projectstore, which would send the game on the iPhone to look for the
	# Mac's Zen server, so what an earlier Zen cook left behind is cooked again from scratch.
	for dir in "$PROJECT_DIR/Saved/Cooked/IOS" "$PROJECT_DIR/Saved/StagedBuilds/IOS"; do
		if [ -f "$dir/ue.projectstore" ]; then
			rm -rf "$dir" || fail "не смог удалить $dir (это данные прошлой сборки, их можно удалить вручную)."
		fi
	done

	touch "$BUILD_MARK"
	note "Собираю версию для iPhone: код, шейдеры, упаковка и подпись..."
	if ! package_game "$LOG_DIR/4-package.log"; then
		# If Unreal asked for a different app ID than expected, sign up that one too and retry once.
		wanted="$(grep -o "No profiles for '[^']*' were found" "$LOG_DIR/4-package.log" | head -n 1 | sed "s/No profiles for '\(.*\)' were found/\1/")"
		if [ -n "$wanted" ] && [ "$wanted" != "$BUNDLE_ID" ] && register_all "$wanted"; then
			note "Повторяю сборку с ID $wanted..."
			if ! package_game "$LOG_DIR/4-package-retry.log"; then
				show_errors "$LOG_DIR/4-package-retry.log" 20
				show_tool_output "$LOG_DIR/4-package-retry.log"
				package_hints "$LOG_DIR/4-package-retry.log"
				fail "сборка для iPhone не прошла (журнал: Saved/InstallLogs/4-package-retry.log)."
			fi
		else
			show_errors "$LOG_DIR/4-package.log" 20
			show_tool_output "$LOG_DIR/4-package.log"
			package_hints "$LOG_DIR/4-package.log"
			fail "сборка для iPhone не прошла (журнал: Saved/InstallLogs/4-package.log)."
		fi
	fi
	good "Игра собрана"
	[ "$STORE_MODE" = 0 ] || check_store_team
fi

# Signs the archive for the App Store and uploads it with the Apple ID in Xcode. Xcode makes the
# App Store certificate and profile itself and gives the build the next free build number.
upload_build() {  # log file
	rm -rf "$STORE_DIR/Upload"
	xcodebuild -exportArchive -archivePath "$ARCHIVE" -exportOptionsPlist "$STORE_DIR/ExportOptions.plist" \
		-exportPath "$STORE_DIR/Upload" -allowProvisioningUpdates </dev/null 2>&1 | tee "$1" \
		| { grep --line-buffered -E 'Progress|[Uu]ploaded|[Uu]pload (succeeded|failed)|EXPORT (SUCCEEDED|FAILED)' || true; }
	return "${PIPESTATUS[0]}"
}

# Explains the usual reasons why App Store Connect does not take the build.
upload_hints() {  # log file
	if grep -q -i -E "personal (development )?team|not support .*(App Store|distribution)" "$1"; then
		rm -f "$STORE_TEAM_FILE"
		note "→ $TEAM — бесплатная команда (Personal Team), а загружать в TestFlight может только платная."
		note "  Запусти bash install_iphone.command --testflight и введи Team ID из раздела Membership details"
		note "  на https://developer.apple.com/account."
	elif grep -q -i -E "No Account|not signed in|session has expired|sign in|authenticat" "$1"; then
		note "→ Войди в Apple ID в Xcode: Settings → Apple Accounts. Если он там уже есть, удали его кнопкой − и добавь снова."
		note "  Потом загрузи сборку ещё раз: bash install_iphone.command --upload-only"
	elif grep -q -i -E "agreement" "$1"; then
		note "→ Apple просит принять новое соглашение: открой https://developer.apple.com/account и согласись с ним."
		note "  Потом загрузи сборку ещё раз: bash install_iphone.command --upload-only"
	elif grep -q -i -E "signing certificate|cloud signing|distribution certificate" "$1"; then
		note "→ Xcode не нашёл, чем подписать игру для App Store. В Xcode открой Settings → Apple Accounts, выбери Apple ID"
		note "  и команду без пометки Personal Team, нажми Manage Certificates… → + → Apple Distribution → Done."
		note "  Потом загрузи сборку ещё раз: bash install_iphone.command --upload-only"
	else
		note "→ Эту же сборку можно попробовать загрузить из Xcode: дважды щёлкни Saved/TestFlight/Kubostrel.xcarchive"
		note "  в Finder и в окне Organizer нажми Distribute App → TestFlight & App Store."
		note "  Если и там ошибка, пришли журнал Saved/InstallLogs/6-upload.log."
	fi
}

# ---------------------------------------------------------------------------------------------
if [ "$STORE_MODE" = 1 ]; then
	step "5/5. Загружаю в App Store Connect"
	if [ "$UPLOAD_ONLY" = 0 ]; then
		found="$("$PY" "$HELPER" find-app --newer "$BUILD_MARK" "$PROJECT_DIR/Binaries/IOS" "$PACKAGE_DIR")"
		[ -n "$found" ] || fail "не нашёл приложение, которое собрал Unreal (журнал: Saved/InstallLogs/4-package.log)."
		open_app "$found"
		note "Приложение: $APP"
		has_game_data "$APP" || fail "в приложении нет данных игры (файлов .pak и .utoc), такое загружать нельзя. Пришли журнал Saved/InstallLogs/4-package.log."
		if ! "$PY" "$HELPER" store-archive "$APP" "$ARCHIVE" "$TEAM" "$UE/Engine/Build/IOS/Resources/UEMetadata/PrivacyInfo.xcprivacy" \
			>"$LOG_DIR/5-archive.log" 2>&1; then
			tail -n 5 "$LOG_DIR/5-archive.log" | sed 's/^/      /'
			fail "не смог подготовить сборку для App Store (журнал: Saved/InstallLogs/5-archive.log)."
		fi
		added="$(awk -F'\t' '$1 == "added" { printf "%s%s", (n++ ? ", " : ""), $2 }' "$LOG_DIR/5-archive.log")"
		[ -z "$added" ] || note "Добавил в приложение то, что требует App Store: $added"
		if awk -F'\t' '$1 == "missing" && $2 == "icon" { found = 1 } END { exit !found }' "$LOG_DIR/5-archive.log"; then
			warn "В приложении нет иконки, App Store Connect может не принять сборку."
		fi
	fi
	APP_ID="$(/usr/libexec/PlistBuddy -c "Print :ApplicationProperties:CFBundleIdentifier" "$ARCHIVE/Info.plist" 2>/dev/null)"
	VERSION="$(/usr/libexec/PlistBuddy -c "Print :ApplicationProperties:CFBundleShortVersionString" "$ARCHIVE/Info.plist" 2>/dev/null)"
	cat >"$STORE_DIR/ExportOptions.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>method</key>
	<string>app-store-connect</string>
	<key>destination</key>
	<string>upload</string>
	<key>teamID</key>
	<string>$TEAM</string>
	<key>signingStyle</key>
	<string>automatic</string>
	<key>manageAppVersionAndBuildNumber</key>
	<true/>
	<key>uploadSymbols</key>
	<false/>
</dict>
</plist>
EOF
	size="$(du -sh "$ARCHIVE" 2>/dev/null | cut -f1)"
	note "Подписываю сборку для App Store и загружаю её${size:+ ($size)}..."
	while ! upload_build "$LOG_DIR/6-upload.log"; do
		show_errors "$LOG_DIR/6-upload.log"
		if grep -q -i -E "No suitable application records|Cannot determine the Apple ID|app record|application record" "$LOG_DIR/6-upload.log"; then
			echo
			note "В App Store Connect ещё нет приложения с Bundle ID $APP_ID. Создай его:"
			note "https://appstoreconnect.apple.com/apps → + → «Новое приложение». Платформа iOS, название Кубострел"
			note "(если занято, добавь слово), язык «Русский», Bundle ID $APP_ID, SKU kubostrel, доступ «Полный»."
			wait_enter "Потом нажми Enter, и я загружу сборку ещё раз..." \
				"в App Store Connect нет приложения $APP_ID. Создай его и запусти: bash install_iphone.command --upload-only"
			continue
		fi
		upload_hints "$LOG_DIR/6-upload.log"
		fail "App Store Connect не принял сборку (журнал: Saved/InstallLogs/6-upload.log)."
	done
	good "Сборка загружена"
	echo
	echo "${GREEN}${BOLD}Готово!${RESET} Кубострел${VERSION:+ $VERSION} загружен в App Store Connect. Заняло $((SECONDS / 60)) мин."
	echo "Через 10–30 минут, когда Apple его обработает, сборка появится в App Store Connect → Кубострел → TestFlight."
	echo "Если Apple пришлёт письмо, что со сборкой что-то не так, пришли его текст."
	echo "Как дать друзьям ссылку на игру, написано в INSTALL.md, раздел «TestFlight»."
	exit 0
fi

# ---------------------------------------------------------------------------------------------
step "5/5. Устанавливаю на iPhone"

# The build can take a while; check again which iPhones are plugged in now.
[ "$INSTALL_ONLY" = 1 ] || wait_for_devices

found="$("$PY" "$HELPER" find-app "$PROJECT_DIR/Binaries/IOS" "$PACKAGE_DIR")"
[ -n "$found" ] || fail "не нашёл собранное приложение. Запусти скрипт без --install-only."
open_app "$found"
APP_ID="$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$APP/Info.plist" 2>/dev/null)"
note "Приложение: $APP"
if ! has_game_data "$APP"; then
	warn "В приложении нет данных игры (файлов .pak и .utoc). Если игра откроется с чёрным экраном, пришли журнал Saved/InstallLogs/4-package.log."
fi

INSTALLED=""
TRUST_NEEDED=0
while IFS=$'\t' read -r id udid name os devmode pairing transport; do
	[ -n "$id" ] || continue
	note "Ставлю на $name..."
	if ! xcrun devicectl device install app --device "$id" "$APP" >"$LOG_DIR/5-install-$udid.log" 2>&1 </dev/null; then
		show_errors "$LOG_DIR/5-install-$udid.log" 6
		if grep -q -i -E "provision|profile|signature|0xe8008012|0xe800801c" "$LOG_DIR/5-install-$udid.log"; then
			warn "Подпись не подходит для $name. Запусти скрипт без --install-only, пока iPhone подключён."
		elif grep -q -i -E "locked|passcode" "$LOG_DIR/5-install-$udid.log"; then
			warn "$name заблокирован. Разблокируй его и запусти скрипт ещё раз."
		else
			warn "Не получилось поставить игру на $name (журнал: Saved/InstallLogs/5-install-$udid.log)."
			newer_ios_hint "$name" "$os"
		fi
		continue
	fi
	INSTALLED="${INSTALLED:+$INSTALLED, }$name"
	if [ -n "$APP_ID" ] && xcrun devicectl device process launch --device "$id" "$APP_ID" >"$LOG_DIR/6-launch-$udid.log" 2>&1 </dev/null; then
		good "Кубострел установлен и запущен на $name"
	else
		good "Кубострел установлен на $name"
		TRUST_NEEDED=1
	fi
done <<EOF
$DEVICES
EOF

[ -n "$INSTALLED" ] || fail "игра не установилась ни на один iPhone (подробности выше)."

echo
echo "${GREEN}${BOLD}Готово!${RESET} Кубострел стоит на: $INSTALLED. Заняло $((SECONDS / 60)) мин."
if [ "$TRUST_NEEDED" = 1 ]; then
	echo
	echo "Сам iPhone пока не открыл игру: он должен один раз доверять твоему Apple ID."
	echo "На iPhone: Настройки → Основные → VPN и управление устройством → (твой Apple ID) → Доверять."
	echo "Потом открой Кубострел на главном экране."
fi
echo
TEAMS="$(list_teams)"
if [ "$(team_kind "$TEAM")" = 0 ]; then
	echo "Подпись платной команды Apple действует год. Потом подключи iPhone и запусти этот файл ещё раз."
else
	echo "Бесплатная подпись Apple действует 7 дней. Потом подключи iPhone и запусти этот файл ещё раз."
fi
