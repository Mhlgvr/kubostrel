#!/bin/bash
# Builds Kubostrel and installs it on the iPhones connected to this Mac, or with --mac starts it on the Mac.
# Run it in Terminal (or double-click it in Finder). INSTALL.md explains the setup.
#
#   bash install_iphone.command                  build, then install on every iPhone on a cable or Wi-Fi
#   bash install_iphone.command --install-only   install the last build again without rebuilding
#   bash install_iphone.command --mac            build and start the game on this Mac (play_mac.command)
#
# Optional environment variables:
#   UE_ROOT=/path/to/UE_5.8          Unreal Engine folder, if the Epic Games Launcher put it elsewhere
#   XCODE_APP=/Applications/Xcode.app   Xcode to build with (by default an installed Xcode 26 is picked)
#   TEAM_ID=ABCDE12345               Apple team to sign with, when Xcode knows several
#   BUNDLE_PREFIX=com.yourname       app ID prefix, if com.mhlgvr is already taken by another Apple ID

set -o pipefail

INSTALL_ONLY=0
MAC_MODE=0
for arg in "$@"; do
	case "$arg" in
		--install-only) INSTALL_ONLY=1 ;;
		--mac) MAC_MODE=1 ;;
		*) echo "Неизвестный параметр: $arg"; exit 2 ;;
	esac
done
if [ "$MAC_MODE" = 1 ] && [ "$INSTALL_ONLY" = 1 ]; then
	echo "--install-only нужен только для iPhone, для Mac запусти без него."
	exit 2
fi

cd "$(dirname "$0")" || exit 1
PROJECT_DIR="$(pwd -P)"
PROJECT="$PROJECT_DIR/Kubostrel.uproject"
ENGINE_INI="$PROJECT_DIR/Config/DefaultEngine.ini"
HELPER="$PROJECT_DIR/Tools/ks_install.py"
LOG_DIR="$PROJECT_DIR/Saved/InstallLogs"
PACKAGE_DIR="$PROJECT_DIR/Saved/Packages"
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

if [ "$MAC_MODE" = 1 ]; then
	STEPS=3
	echo "${BOLD}Кубострел: запуск на Mac${RESET}"
	echo "Первый запуск долгий: Unreal компилирует игру и готовит шейдеры. Mac не уснёт, пока скрипт работает."
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
if ! xcrun --sdk "$METAL_SDK" metal --version >/dev/null 2>&1; then
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

# ---------------------------------------------------------------------------------------------
step "3/5. Подпись: Apple ID и iPhone"

ini_get() { "$PY" "$HELPER" get-ini "$ENGINE_INI" "$1" "$2" | tr -d '\r'; }
ini_set() { "$PY" "$HELPER" set-ini "$ENGINE_INI" "$1" "$2" "$3" || fail "не смог записать $2 в Config/DefaultEngine.ini."; }

TEAMS="$("$PY" "$HELPER" teams 2>>"$LOG_DIR/teams.log")"
INI_TEAM="$(ini_get "$XCODE_SECTION" ModernSigningTeam)"
if [ -n "$TEAM_ID" ]; then
	TEAM="$TEAM_ID"
elif [ -n "$INI_TEAM" ] && printf '%s\n' "$TEAMS" | cut -f1 | grep -qx "$INI_TEAM"; then
	TEAM="$INI_TEAM"
elif [ -n "$TEAMS" ]; then
	# A paid team signs for a year, a free Personal Team for 7 days, so a paid one wins.
	TEAM="$(printf '%s\n' "$TEAMS" | awk -F'\t' '$3 == "0" { print $1; exit }')"
	[ -n "$TEAM" ] || TEAM="$(printf '%s\n' "$TEAMS" | head -n 1 | cut -f1)"
elif [ -n "$INI_TEAM" ]; then
	TEAM="$INI_TEAM"
else
	fail "в Xcode нет Apple ID. Открой Xcode → Settings → Apple Accounts (или Accounts), нажми + и войди своим Apple ID, потом запусти скрипт снова."
fi
TEAM_NAME="$(printf '%s\n' "$TEAMS" | awk -F'\t' -v t="$TEAM" '$1 == t && $2 != "-" { print $2; exit }')"
note "Команда для подписи: ${TEAM_NAME:-$TEAM} ($TEAM)"
if [ "$(printf '%s\n' "$TEAMS" | grep -c .)" -gt 1 ] && [ -z "$TEAM_ID" ]; then
	note "Xcode знает несколько команд. Другую можно выбрать так: TEAM_ID=XXXXXXXXXX bash install_iphone.command"
fi
[ "$TEAM" = "$INI_TEAM" ] || ini_set "$XCODE_SECTION" ModernSigningTeam "$TEAM"
[ "$(ini_get "$IOS_SECTION" IOSTeamID)" = "$TEAM" ] || ini_set "$IOS_SECTION" IOSTeamID "$TEAM"

if [ -n "$BUNDLE_PREFIX" ]; then
	printf '%s' "$BUNDLE_PREFIX" | LC_ALL=C grep -Eq '^[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)*$' \
		|| fail "в BUNDLE_PREFIX можно писать только латинские буквы, цифры, точки и дефисы, например com.ivanov."
	ini_set "$XCODE_SECTION" ModernSigningPrefix "$BUNDLE_PREFIX"
	ini_set "$IOS_SECTION" BundleIdentifier "$BUNDLE_PREFIX.Kubostrel"
fi
PREFIX="$(ini_get "$XCODE_SECTION" ModernSigningPrefix)"
BUNDLE_ID="${PREFIX:-com.mhlgvr}.Kubostrel"

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

wait_for_devices
while IFS=$'\t' read -r id udid name os devmode pairing transport; do
	[ -n "$id" ] && note "iPhone: $name, iOS $os ($( [ "$transport" = wired ] && echo "по кабелю" || echo "по Wi-Fi" ))"
done <<EOF
$DEVICES
EOF

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
	fi
	if grep -q -i -E "cannot be registered to your development team|not available. Change your bundle identifier" "$log"; then
		note "→ ID $3 занят другим Apple ID. Придумай любое слово латиницей и запусти так:"
		note "  BUNDLE_PREFIX=com.ivanov bash install_iphone.command"
	fi
	if grep -q -i -E "maximum number of|reached the limit|limit of [0-9]" "$log"; then
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

if [ "$INSTALL_ONLY" = 0 ]; then
	note "Если Mac спросит пароль для «codesign», введи пароль от Mac и нажми «Всегда разрешать»."
	register_all "$BUNDLE_ID" || fail "iPhone не зарегистрирован для подписи (подробности выше)."
	good "iPhone зарегистрирован, подпись готова"
fi

# ---------------------------------------------------------------------------------------------
UAT_FILTER='\*{6,}|Packages Remain|Compiling [0-9]+ shaders|Shaders Left|[Ee]rror:|ERROR:|BUILD SUCCESSFUL|BUILD FAILED|AutomationTool exiting|ExitCode='

# Explains the usual reasons why the iPhone build fails.
package_hints() {  # log file
	if grep -q -i -E "not a valid platform|IOS SDK|Platform IOS is not" "$1"; then
		note "→ Unreal не видит iOS: проверь, что в Epic Games Launcher у версии 5.8 отмечена поддержка iOS, и что используется Xcode 26."
	fi
	if grep -q -E "errSecInternalComponent|User interaction is not allowed|unable to build chain" "$1"; then
		note "→ macOS не дал подписать приложение. Запусти скрипт снова и, когда Mac спросит пароль для «codesign», введи пароль от Mac и нажми «Всегда разрешать»."
	fi
	if grep -q -E "No profiles for|requires a development team|No signing certificate" "$1"; then
		note "→ Проблема с подписью: открой Xcode → Settings → Apple Accounts, проверь, что Apple ID на месте, и запусти скрипт снова."
	fi
	if grep -q -i "No space left on device" "$1"; then
		note "→ Закончилось место на диске. Освободи хотя бы 20 ГБ."
	fi
}

package_game() {  # log file
	"$UE/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun \
		-project="$PROJECT" -target=Kubostrel -platform=IOS -clientconfig=Development \
		-nop4 -utf8output -unattended -nocompile -nocompileuat -nocompileeditor -skipbuildeditor \
		-unrealexe="$UE_EDITOR" \
		-build -cook -stage -pak -iostore -compressed -package \
		-archive -archivedirectory="$PACKAGE_DIR" </dev/null 2>&1 | tee "$1" | { grep --line-buffered -E "$UAT_FILTER" || true; }
	return "${PIPESTATUS[0]}"
}

if [ "$INSTALL_ONLY" = 0 ]; then
	step "4/5. Собираю игру"

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

	note "Собираю версию для iPhone: код, шейдеры, упаковка и подпись..."
	if ! package_game "$LOG_DIR/4-package.log"; then
		# If Unreal asked for a different app ID than expected, sign up that one too and retry once.
		wanted="$(grep -o "No profiles for '[^']*' were found" "$LOG_DIR/4-package.log" | head -n 1 | sed "s/No profiles for '\(.*\)' were found/\1/")"
		if [ -n "$wanted" ] && [ "$wanted" != "$BUNDLE_ID" ] && register_all "$wanted"; then
			note "Повторяю сборку с ID $wanted..."
			if ! package_game "$LOG_DIR/4-package-retry.log"; then
				show_errors "$LOG_DIR/4-package-retry.log" 20
				package_hints "$LOG_DIR/4-package-retry.log"
				fail "сборка для iPhone не прошла (журнал: Saved/InstallLogs/4-package-retry.log)."
			fi
		else
			show_errors "$LOG_DIR/4-package.log" 20
			package_hints "$LOG_DIR/4-package.log"
			fail "сборка для iPhone не прошла (журнал: Saved/InstallLogs/4-package.log)."
		fi
	fi
	good "Игра собрана"
fi

# ---------------------------------------------------------------------------------------------
step "5/5. Устанавливаю на iPhone"

# The build can take a while; check again which iPhones are plugged in now.
[ "$INSTALL_ONLY" = 1 ] || wait_for_devices

APP="$("$PY" "$HELPER" find-app "$PACKAGE_DIR" "$PROJECT_DIR/Binaries/IOS")"
[ -n "$APP" ] || fail "не нашёл собранное приложение. Запусти скрипт без --install-only."
case "$APP" in
	*.ipa)
		rm -rf "$LOG_DIR/ipa" && mkdir -p "$LOG_DIR/ipa"
		ditto -x -k "$APP" "$LOG_DIR/ipa" || fail "не смог распаковать $APP."
		APP="$(ls -d "$LOG_DIR/ipa/Payload/"*.app 2>/dev/null | head -n 1)"
		[ -n "$APP" ] || fail "в архиве нет приложения."
		;;
esac
APP_ID="$(/usr/libexec/PlistBuddy -c "Print :CFBundleIdentifier" "$APP/Info.plist" 2>/dev/null)"
note "Приложение: $APP"

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
echo "Бесплатная подпись Apple действует 7 дней. Потом подключи iPhone и запусти этот файл ещё раз."
