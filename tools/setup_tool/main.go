//go:build windows

package main

import (
	"archive/zip"
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"runtime"
	"sort"
	"strings"
	"sync"
	"syscall"
	"time"
	"unsafe"
)

const (
	appVersion        = "0.5.15"
	appTitle          = "OpenTurbine Setup Tool"
	ecuBaseURL        = "http://192.168.4.1"
	defaultPackageURL = "https://github.com/elia179/OpenTurbine/releases/latest/download/OpenTurbine_Recommended.zip"
)

var (
	colBg         = rgb(17, 21, 24)
	colHeader     = rgb(23, 28, 33)
	colPanel      = rgb(30, 37, 43)
	colPanelSoft  = rgb(37, 45, 52)
	colBorder     = rgb(61, 75, 90)
	colBorderSoft = rgb(44, 54, 64)
	colText       = rgb(231, 239, 245)
	colTextMuted  = rgb(170, 185, 199)
	colTextSoft   = rgb(131, 149, 169)
	colAccent     = rgb(238, 118, 32)
	colAccent2    = rgb(245, 158, 11)
	colAccentDark = rgb(124, 63, 24)
	colInfoBg     = rgb(32, 40, 58)
	colInfoBorder = rgb(54, 82, 124)
	colInfoText   = rgb(215, 229, 255)
	colTitleBar   = rgb(58, 62, 66)
)

var webAssets = []string{
	"app.js.gz",
	"calibration.html.gz",
	"config.html.gz",
	"hardware.html.gz",
	"index.html.gz",
	"log.html.gz",
	"sequence.html.gz",
	"style.css.gz",
	"tools.html.gz",
	"theme.js.gz",
}

type AppConfig struct {
	PackageURL string `json:"package_url"`
}

type Manifest struct {
	Project     string                    `json:"project"`
	Version     string                    `json:"version"`
	Recommended bool                      `json:"recommended"`
	Targets     map[string]ManifestTarget `json:"targets"`
	FirmwareOTA string                    `json:"firmware_ota"`
	WebAssets   string                    `json:"web_assets"`
}

type ManifestTarget struct {
	Chip        string       `json:"chip"`
	USBFlash    []FlashEntry `json:"usb_flash"`
	FirmwareOTA string       `json:"firmware_ota"`
	WebAssets   string       `json:"web_assets"`
}

type FlashEntry struct {
	Address string `json:"address"`
	File    string `json:"file"`
	SHA256  string `json:"sha256,omitempty"`
}

type Package struct {
	Root     string
	Manifest Manifest
}

type App struct {
	workDir string
	config  AppConfig
}

type Job struct {
	app        *App
	mode       string
	continueCh chan struct{}
	actionCh   chan string
	backupPath string
	logs       []string
	mu         sync.Mutex
}

func main() {
	runtime.LockOSThread()
	app := newApp()
	runGUI(app)
}

func newApp() *App {
	dir := setupToolDataDir()
	_ = os.MkdirAll(dir, 0755)
	cfg := AppConfig{PackageURL: defaultPackageURL}
	exe, _ := os.Executable()
	if exe != "" {
		p := filepath.Join(filepath.Dir(exe), "openturbine_setup_tool.json")
		if data, err := os.ReadFile(p); err == nil {
			_ = json.Unmarshal(data, &cfg)
		}
	}
	return &App{workDir: dir, config: cfg}
}

// ---------------- Native modern Win32 UI ----------------

var (
	kernel32 = syscall.NewLazyDLL("kernel32.dll")
	user32   = syscall.NewLazyDLL("user32.dll")
	gdi32    = syscall.NewLazyDLL("gdi32.dll")
	shell32  = syscall.NewLazyDLL("shell32.dll")
	dwmapi   = syscall.NewLazyDLL("dwmapi.dll")

	procGetModuleHandleW   = kernel32.NewProc("GetModuleHandleW")
	procGetCurrentThreadId = kernel32.NewProc("GetCurrentThreadId")
	procRegisterClassExW   = user32.NewProc("RegisterClassExW")
	procCreateWindowExW    = user32.NewProc("CreateWindowExW")
	procDefWindowProcW     = user32.NewProc("DefWindowProcW")
	procShowWindow         = user32.NewProc("ShowWindow")
	procUpdateWindow       = user32.NewProc("UpdateWindow")
	procGetMessageW        = user32.NewProc("GetMessageW")
	procTranslateMessage   = user32.NewProc("TranslateMessage")
	procDispatchMessageW   = user32.NewProc("DispatchMessageW")
	procPostQuitMessage    = user32.NewProc("PostQuitMessage")
	procPostMessageW       = user32.NewProc("PostMessageW")
	procGetClientRect      = user32.NewProc("GetClientRect")
	procInvalidateRect     = user32.NewProc("InvalidateRect")
	procBeginPaint         = user32.NewProc("BeginPaint")
	procEndPaint           = user32.NewProc("EndPaint")
	procSetWindowTextW     = user32.NewProc("SetWindowTextW")
	procLoadCursorW        = user32.NewProc("LoadCursorW")
	procOpenClipboard      = user32.NewProc("OpenClipboard")
	procEmptyClipboard     = user32.NewProc("EmptyClipboard")
	procCloseClipboard     = user32.NewProc("CloseClipboard")
	procSetClipboardData   = user32.NewProc("SetClipboardData")
	procGlobalAlloc        = kernel32.NewProc("GlobalAlloc")
	procGlobalLock         = kernel32.NewProc("GlobalLock")
	procGlobalUnlock       = kernel32.NewProc("GlobalUnlock")

	procCreateFontW      = gdi32.NewProc("CreateFontW")
	procCreateSolidBrush = gdi32.NewProc("CreateSolidBrush")
	procCreatePen        = gdi32.NewProc("CreatePen")
	procSelectObject     = gdi32.NewProc("SelectObject")
	procDeleteObject     = gdi32.NewProc("DeleteObject")
	procFillRect         = user32.NewProc("FillRect")
	procRoundRect        = gdi32.NewProc("RoundRect")
	procRectangle        = gdi32.NewProc("Rectangle")
	procDrawTextW        = user32.NewProc("DrawTextW")
	procSetTextColor     = gdi32.NewProc("SetTextColor")
	procSetBkMode        = gdi32.NewProc("SetBkMode")
	procMoveToEx         = gdi32.NewProc("MoveToEx")
	procLineTo           = gdi32.NewProc("LineTo")

	procShellExecuteW         = shell32.NewProc("ShellExecuteW")
	procDwmSetWindowAttribute = dwmapi.NewProc("DwmSetWindowAttribute")
)

const (
	wsOverlappedWindow = 0x00CF0000
	wsVisible          = 0x10000000
	createNoWindow     = 0x08000000

	wmCreate        = 0x0001
	wmDestroy       = 0x0002
	wmSize          = 0x0005
	wmPaint         = 0x000F
	wmLButtonDown   = 0x0201
	wmMouseMove     = 0x0200
	wmSetCursor     = 0x0020
	wmUser          = 0x0400
	wmAppUpdate     = wmUser + 10
	wmAppInvalidate = wmUser + 11

	swShowDefault = 10

	cfUnicodeText = 13
	gmemMoveable  = 0x0002

	dtLeft       = 0x00000000
	dtCenter     = 0x00000001
	dtRight      = 0x00000002
	dtVCenter    = 0x00000004
	dtWordBreak  = 0x00000010
	dtSingleLine = 0x00000020
	dtNoPrefix   = 0x00000800

	transparent = 1
	psSolid     = 0

	cursorArrow = 32512
	cursorHand  = 32649
)

type rect struct{ left, top, right, bottom int32 }
type point struct{ x, y int32 }
type msg struct {
	hwnd           uintptr
	message        uint32
	wParam, lParam uintptr
	time           uint32
	pt             point
}
type paintStruct struct {
	hdc         uintptr
	fErase      int32
	rcPaint     rect
	fRestore    int32
	fIncUpdate  int32
	rgbReserved [32]byte
}
type wndClassEx struct {
	cbSize        uint32
	style         uint32
	lpfnWndProc   uintptr
	cbClsExtra    int32
	cbWndExtra    int32
	hInstance     uintptr
	hIcon         uintptr
	hCursor       uintptr
	hbrBackground uintptr
	lpszMenuName  *uint16
	lpszClassName *uint16
	hIconSm       uintptr
}

type screenKind int

const (
	screenHome screenKind = iota
	screenSafety
	screenRunning
	screenWait
	screenDone
	screenError
	screenDriverHelp
)

type clickZone struct {
	r      rect
	action string
}

type NativeUI struct {
	app         *App
	hwnd        uintptr
	uiThreadID  uint32
	fontTitle   uintptr
	fontHeading uintptr
	fontBody    uintptr
	fontSmall   uintptr
	fontButton  uintptr

	mu          sync.Mutex
	screen      screenKind
	title       string
	subtitle    string
	body        string
	detail      string
	mode        string
	step        int
	totalSteps  int
	progress    int
	primary     string
	secondary   string
	pendingMode string
	backupPath  string
	logs        []string
	showDetails bool
	activeJob   *Job
	zones       []clickZone

	pending *uiUpdate
}

type uiUpdate struct {
	screen     screenKind
	title      string
	subtitle   string
	body       string
	detail     string
	step       int
	totalSteps int
	progress   int
	primary    string
	secondary  string
	backupPath string
	done       bool
	mode       string
	appendLog  []string
}

var globalUI *NativeUI

func runGUI(app *App) {
	ui := &NativeUI{app: app}
	globalUI = ui
	threadID, _, _ := procGetCurrentThreadId.Call()
	ui.uiThreadID = uint32(threadID)
	hInst, _, _ := procGetModuleHandleW.Call(0)
	arrow, _, _ := procLoadCursorW.Call(0, cursorArrow)
	className := utf16Ptr("OpenTurbineSetupToolModernWindow")
	wc := wndClassEx{
		cbSize:        uint32(unsafe.Sizeof(wndClassEx{})),
		lpfnWndProc:   syscall.NewCallback(wndProc),
		hInstance:     hInst,
		hCursor:       arrow,
		hbrBackground: 0,
		lpszClassName: className,
	}
	procRegisterClassExW.Call(uintptr(unsafe.Pointer(&wc)))
	hwnd, _, _ := procCreateWindowExW.Call(
		0,
		uintptr(unsafe.Pointer(className)),
		uintptr(unsafe.Pointer(utf16Ptr(appTitle))),
		wsOverlappedWindow|wsVisible,
		0x80000000, 0x80000000, 900, 740,
		0, 0, hInst, 0,
	)
	ui.hwnd = hwnd
	setTitleBarColors(hwnd)
	procShowWindow.Call(hwnd, swShowDefault)
	procUpdateWindow.Call(hwnd)
	var m msg
	for {
		ret, _, _ := procGetMessageW.Call(uintptr(unsafe.Pointer(&m)), 0, 0, 0)
		if int32(ret) <= 0 {
			break
		}
		procTranslateMessage.Call(uintptr(unsafe.Pointer(&m)))
		procDispatchMessageW.Call(uintptr(unsafe.Pointer(&m)))
	}
}

func wndProc(hwnd uintptr, msgID uint32, wParam, lParam uintptr) uintptr {
	ui := globalUI
	switch msgID {
	case wmCreate:
		ui.hwnd = hwnd
		ui.createResources()
		ui.showPreparing()
		go ui.prepareThenShowHome()
		return 0
	case wmPaint:
		ui.paint()
		return 0
	case wmSize:
		ui.invalidate()
		return 0
	case wmLButtonDown:
		x := int(int16(lParam & 0xffff))
		y := int(int16((lParam >> 16) & 0xffff))
		ui.click(x, y)
		return 0
	case wmSetCursor:
		// Leave the cursor simple and predictable.
		arrow, _, _ := procLoadCursorW.Call(0, cursorArrow)
		user32.NewProc("SetCursor").Call(arrow)
		return 1
	case wmAppUpdate:
		ui.applyPending()
		return 0
	case wmAppInvalidate:
		procInvalidateRect.Call(hwnd, 0, 1)
		return 0
	case wmDestroy:
		procPostQuitMessage.Call(0)
		return 0
	}
	ret, _, _ := procDefWindowProcW.Call(hwnd, uintptr(msgID), wParam, lParam)
	return ret
}

func (ui *NativeUI) createResources() {
	ui.fontTitle = createFont(-26, 700)
	ui.fontHeading = createFont(-23, 700)
	ui.fontBody = createFont(-18, 400)
	ui.fontSmall = createFont(-15, 400)
	ui.fontButton = createFont(-18, 700)
}

func (ui *NativeUI) showHome() {
	ui.mu.Lock()
	ui.screen = screenHome
	ui.title = appTitle
	ui.subtitle = "Ready. Simple setup and updates for OpenTurbine boards."
	ui.body = ""
	ui.detail = ""
	ui.mode = ""
	ui.step = 0
	ui.totalSteps = 0
	ui.progress = 0
	ui.primary = ""
	ui.secondary = ""
	ui.pendingMode = ""
	ui.showDetails = false
	ui.activeJob = nil
	ui.mu.Unlock()
	ui.invalidate()
}

func (ui *NativeUI) showPreparing() {
	ui.mu.Lock()
	ui.screen = screenRunning
	ui.title = "Preparing OpenTurbine Setup Tool"
	ui.subtitle = "Stay connected to your normal internet Wi‑Fi."
	ui.body = "Checking setup files. The tool will download anything missing before it opens."
	ui.detail = "After this, you will only choose between setting up a new board or updating an existing board."
	ui.mode = ""
	ui.step = 1
	ui.totalSteps = 2
	ui.progress = 8
	ui.primary = ""
	ui.secondary = ""
	ui.pendingMode = ""
	ui.showDetails = false
	ui.activeJob = nil
	ui.logs = nil
	ui.mu.Unlock()
	ui.invalidate()
}

func (ui *NativeUI) prepareThenShowHome() {
	logf := func(line string, percent int) {
		if percent < 8 {
			percent = 8
		}
		if percent > 96 {
			percent = 96
		}
		ui.update(uiUpdate{screen: screenRunning, title: "Preparing OpenTurbine Setup Tool", subtitle: "Stay connected to your normal internet Wi‑Fi.", body: line, detail: "The setup tool will open automatically when the required files are ready.", step: 1, totalSteps: 2, progress: percent, appendLog: []string{line}})
	}
	logf("Checking for the recommended OpenTurbine files.", 12)
	if _, err := ui.app.ensurePackageWithProgress(logf); err != nil {
		body := "The tool could not prepare the required OpenTurbine files.\n\nStay connected to your normal internet Wi-Fi and open the tool again.\n\nFor now, OpenTurbine also needs a GitHub Release asset named OpenTurbine_Recommended.zip."
		ui.update(uiUpdate{screen: screenError, title: "Setup files not ready", subtitle: "The tool could not download or check the required files.", body: body, detail: oneLine(err.Error()), step: 0, totalSteps: 0, progress: 0, primary: "Back to start", secondary: "", done: true, appendLog: []string{"ERROR: " + err.Error()}})
		return
	}
	ui.update(uiUpdate{screen: screenRunning, title: "Preparing OpenTurbine Setup Tool", subtitle: "Setup files ready.", body: "Required files are ready. Opening the setup tool.", detail: "", step: 2, totalSteps: 2, progress: 100, appendLog: []string{"Required files are ready."}})
	time.Sleep(500 * time.Millisecond)
	ui.update(uiUpdate{screen: screenHome, title: appTitle, subtitle: "Ready. Simple setup and updates for OpenTurbine boards."})
}

func (ui *NativeUI) showSafety(mode string) {
	subtitle := "For a blank board connected by USB."
	body := "Before continuing:\n\nA blank ESP32 can briefly put pins in unexpected states while it is being erased, flashed, or restarted.\n\nWatch any connected outputs, especially relays, pumps, starters, igniters, servos, and valves. Disconnect anything that could move, heat, spark, or start fuel flow.\n\nAfter flashing, check Hardware, Config, Calibration, and Sequence before connecting engine outputs."
	if mode == "update" {
		subtitle = "For a board that already has OpenTurbine installed."
		body = "Before continuing:\n\nStop the engine and make sure no actuator test is running.\n\nKeep power stable while the board software and dashboard files are updated. The tool will back up ecu_config.json before it changes anything.\n\nAfter updating, check Hardware, Config, Calibration, and Sequence before using fuel."
	}
	ui.update(uiUpdate{
		screen:     screenSafety,
		title:      "Safety check",
		subtitle:   subtitle,
		body:       body,
		mode:       mode,
		primary:    "I have done this — continue",
		secondary:  "Back",
		appendLog:  nil,
		progress:   0,
		totalSteps: 0,
		step:       0,
	})
}

func (ui *NativeUI) startJob(mode string) {
	if ui.activeJob != nil {
		return
	}
	job := &Job{app: ui.app, mode: mode, continueCh: make(chan struct{}), actionCh: make(chan string)}
	ui.mu.Lock()
	ui.activeJob = job
	ui.logs = nil
	ui.showDetails = false
	ui.mu.Unlock()
	if mode == "new" {
		go job.runNewBoard()
	} else {
		go job.runExistingUpdate()
	}
}

func (ui *NativeUI) click(x, y int) {
	ui.mu.Lock()
	zones := append([]clickZone(nil), ui.zones...)
	screen := ui.screen
	mode := ui.pendingMode
	job := ui.activeJob
	ui.mu.Unlock()
	for _, z := range zones {
		if pointInRect(x, y, z.r) {
			switch z.action {
			case "new":
				ui.showSafety("new")
			case "update":
				ui.showSafety("update")
			case "start":
				ui.startJob(mode)
			case "back":
				ui.showHome()
			case "continue":
				if job != nil {
					select {
					case job.continueCh <- struct{}{}:
					default:
					}
				}
			case "home":
				ui.showHome()
			case "openBackup":
				ui.openBackupFolder()
			case "copyLog":
				ui.copyLogToClipboard()
			case "driverCP210x":
				if job != nil {
					select {
					case job.actionCh <- "cp210x":
					default:
					}
				}
			case "driverCH340":
				if job != nil {
					select {
					case job.actionCh <- "ch340":
					default:
					}
				}
			case "retryUSB":
				if job != nil {
					select {
					case job.actionCh <- "retry":
					default:
					}
				}
			case "cancelUSB":
				if job != nil {
					select {
					case job.actionCh <- "cancel":
					default:
					}
				}
			case "details":
				ui.mu.Lock()
				ui.showDetails = !ui.showDetails
				ui.mu.Unlock()
				ui.invalidate()
			case "close":
				procPostQuitMessage.Call(0)
			}
			return
		}
	}
	switch screen {
	case screenHome:
		if y >= 170 && y <= 395 {
			if x < 450 {
				ui.showSafety("new")
				return
			}
			ui.showSafety("update")
			return
		}
	case screenSafety:
		if y >= 520 {
			if x < 330 {
				ui.showHome()
				return
			}
			ui.startJob(mode)
			return
		}
	case screenWait:
		if y >= 520 && job != nil {
			select {
			case job.continueCh <- struct{}{}:
			default:
			}
			return
		}
	case screenError, screenDone:
		if y >= 520 && x > 500 {
			ui.showHome()
			return
		}
	}
	_ = screen
}

func (ui *NativeUI) openBackupFolder() {
	ui.mu.Lock()
	p := ui.backupPath
	ui.mu.Unlock()
	if p == "" {
		p = ui.app.workDir
	}
	if fileExists(p) {
		p = filepath.Dir(p)
	}
	_ = os.MkdirAll(p, 0755)
	procShellExecuteW.Call(ui.hwnd, uintptr(unsafe.Pointer(utf16Ptr("open"))), uintptr(unsafe.Pointer(utf16Ptr(p))), 0, 0, 1)
}

func (ui *NativeUI) copyLogToClipboard() {
	ui.mu.Lock()
	title := ui.title
	subtitle := ui.subtitle
	body := ui.body
	detail := ui.detail
	logs := append([]string(nil), ui.logs...)
	ui.mu.Unlock()

	var b strings.Builder
	b.WriteString("OpenTurbine Setup Tool " + appVersion + "\r\n")
	if title != "" {
		b.WriteString(title + "\r\n")
	}
	if subtitle != "" {
		b.WriteString(subtitle + "\r\n")
	}
	if body != "" {
		b.WriteString("\r\n" + body + "\r\n")
	}
	if detail != "" {
		b.WriteString("\r\n" + detail + "\r\n")
	}
	if len(logs) > 0 {
		b.WriteString("\r\nLog:\r\n")
		for _, line := range logs {
			b.WriteString(line + "\r\n")
		}
	}
	if err := setClipboardText(ui.hwnd, b.String()); err != nil {
		return
	}
	ui.mu.Lock()
	ui.detail = "Log copied to clipboard."
	ui.mu.Unlock()
	ui.invalidate()
}

func (ui *NativeUI) update(u uiUpdate) {
	ui.mu.Lock()
	ui.pending = &u
	ui.mu.Unlock()
	procPostMessageW.Call(ui.hwnd, wmAppUpdate, 0, 0)
}

func (ui *NativeUI) applyPending() {
	ui.mu.Lock()
	p := ui.pending
	ui.pending = nil
	if p != nil {
		ui.screen = p.screen
		ui.title = p.title
		ui.subtitle = p.subtitle
		ui.body = p.body
		ui.detail = p.detail
		ui.step = p.step
		ui.totalSteps = p.totalSteps
		ui.progress = p.progress
		ui.primary = p.primary
		ui.secondary = p.secondary
		if p.mode != "" {
			ui.mode = p.mode
			if p.screen == screenSafety {
				ui.pendingMode = p.mode
			}
		}
		if p.backupPath != "" {
			ui.backupPath = p.backupPath
		}
		if len(p.appendLog) > 0 {
			ui.logs = append(ui.logs, p.appendLog...)
		}
		if p.done {
			ui.activeJob = nil
		}
		if p.screen == screenHome {
			ui.body = ""
			ui.detail = ""
			ui.mode = ""
			ui.step = 0
			ui.totalSteps = 0
			ui.progress = 0
			ui.primary = ""
			ui.secondary = ""
			ui.pendingMode = ""
			ui.backupPath = ""
			ui.showDetails = false
			ui.activeJob = nil
		}
	}
	ui.mu.Unlock()
	ui.invalidate()
}

func (ui *NativeUI) invalidate() {
	if ui.hwnd != 0 {
		procPostMessageW.Call(ui.hwnd, wmAppInvalidate, 0, 0)
	}
}

func (ui *NativeUI) paint() {
	var ps paintStruct
	hdc, _, _ := procBeginPaint.Call(ui.hwnd, uintptr(unsafe.Pointer(&ps)))
	defer procEndPaint.Call(ui.hwnd, uintptr(unsafe.Pointer(&ps)))
	var cr rect
	procGetClientRect.Call(ui.hwnd, uintptr(unsafe.Pointer(&cr)))
	w := int(cr.right - cr.left)
	h := int(cr.bottom - cr.top)
	if w < 760 {
		w = 760
	}
	if h < 560 {
		h = 560
	}

	ui.mu.Lock()
	s := ui.screen
	title := ui.title
	subtitle := ui.subtitle
	body := ui.body
	detail := ui.detail
	step := ui.step
	total := ui.totalSteps
	progress := ui.progress
	primary := ui.primary
	secondary := ui.secondary
	backupPath := ui.backupPath
	secondaryState := ui.secondary
	mode := ui.mode
	logs := append([]string(nil), ui.logs...)
	showDetails := ui.showDetails
	ui.zones = nil
	ui.mu.Unlock()

	fill(hdc, rect{0, 0, int32(w), int32(h)}, colBg)
	fill(hdc, rect{0, 0, int32(w), 86}, colHeader)
	line(hdc, 0, 86, w, 86, colBorderSoft, 1)
	text(hdc, title, rect{34, 14, int32(w - 34), 54}, ui.fontTitle, colText, dtLeft|dtSingleLine|dtNoPrefix)
	text(hdc, subtitle, rect{36, 56, int32(w - 36), 80}, ui.fontSmall, colTextMuted, dtLeft|dtSingleLine|dtNoPrefix)

	switch s {
	case screenHome:
		ui.paintHome(hdc, w, h)
	case screenSafety:
		ui.paintCardScreen(hdc, w, h, body, detail, step, total, progress, primary, secondary, false, false, logs, showDetails)
	case screenRunning:
		ui.paintCardScreen(hdc, w, h, body, detail, step, total, progress, "", "", true, true, logs, showDetails)
	case screenWait:
		ui.paintCardScreen(hdc, w, h, body, detail, step, total, progress, primary, "", false, true, logs, showDetails)
	case screenDone:
		if backupPath != "" && !strings.Contains(body, backupPath) {
			body += "\n\nSettings backup:\n" + backupPath
		}
		secondary := "Open log folder"
		if backupPath != "" {
			secondary = "Open backup folder"
		}
		ui.paintCardScreen(hdc, w, h, body, detail, step, total, 100, "Back to start", secondary, false, true, logs, showDetails)
	case screenDriverHelp:
		ui.paintDriverHelp(hdc, w, h, body, detail, logs, showDetails)
	case screenError:
		errorSecondary := secondaryState
		if errorSecondary == "" {
			errorSecondary = "Open folder"
		}
		ui.paintCardScreen(hdc, w, h, body, detail, step, total, progress, "Back to start", errorSecondary, false, true, logs, showDetails)
	}

	// Footer is intentionally simple.
	text(hdc, "OpenTurbine Setup Tool "+appVersion, rect{34, int32(h - 36), int32(w - 34), int32(h - 12)}, ui.fontSmall, colTextSoft, dtLeft|dtSingleLine|dtNoPrefix)
	if mode != "" && s != screenHome {
		label := "New board setup"
		if mode == "update" {
			label = "Existing board update"
		}
		text(hdc, label, rect{34, int32(h - 36), int32(w - 34), int32(h - 12)}, ui.fontSmall, colTextSoft, dtRight|dtSingleLine|dtNoPrefix)
	}
}

func (ui *NativeUI) paintHome(hdc uintptr, w, h int) {
	text(hdc, "Choose one option", rect{36, 118, int32(w - 36), 154}, ui.fontHeading, colText, dtLeft|dtSingleLine|dtNoPrefix)
	margin := 34
	gap := 24
	cardW := (w - margin*2 - gap) / 2
	if cardW > 390 {
		cardW = 390
	}
	x1 := margin
	x2 := x1 + cardW + gap
	y := 178
	cardH := 268
	r1 := rect{int32(x1), int32(y), int32(x1 + cardW), int32(y + cardH)}
	r2 := rect{int32(x2), int32(y), int32(x2 + cardW), int32(y + cardH)}
	ui.drawActionCard(hdc, r1, "Set up a new board", "For a blank ESP32 or ESP32-S3 board.\n\nThe tool will install OpenTurbine using USB.", "Use this for new boards", "new")
	ui.drawActionCard(hdc, r2, "Update my OpenTurbine board", "For a board that already runs OpenTurbine.\n\nThe tool will save your settings, then update by Wi-Fi.", "Use this for existing boards", "update")
	noteY := y + cardH + 34
	note := "During update the tool will tell you exactly when to stay on normal internet Wi-Fi and when to connect to the board Wi-Fi. The board Wi-Fi name may be OpenTurbine or your own engine/project name."
	drawPanel(hdc, rect{34, int32(noteY), int32(w - 34), int32(noteY + 128)}, colInfoBg, colInfoBorder, 18)
	text(hdc, note, rect{56, int32(noteY + 22), int32(w - 56), int32(noteY + 112)}, ui.fontBody, colInfoText, dtLeft|dtWordBreak|dtNoPrefix)
}

func (ui *NativeUI) drawActionCard(hdc uintptr, r rect, heading, body, button, action string) {
	drawPanel(hdc, r, colPanel, colBorderSoft, 24)
	text(hdc, heading, rect{r.left + 24, r.top + 26, r.right - 24, r.top + 64}, ui.fontHeading, colText, dtLeft|dtSingleLine|dtNoPrefix)
	text(hdc, body, rect{r.left + 24, r.top + 82, r.right - 24, r.bottom - 84}, ui.fontBody, colTextMuted, dtLeft|dtWordBreak|dtNoPrefix)
	br := rect{r.left + 24, r.bottom - 64, r.right - 24, r.bottom - 22}
	drawButton(hdc, br, button, ui.fontButton, true)
	ui.addZone(br, action)
	ui.addZone(r, action)
}

func (ui *NativeUI) paintDriverHelp(hdc uintptr, w, h int, body, detail string, logs []string, showDetails bool) {
	card := rect{34, 112, int32(w - 34), int32(h - 78)}
	drawPanel(hdc, card, colPanel, colBorderSoft, 24)
	top := int(card.top) + 28
	text(hdc, body, rect{card.left + 28, int32(top), card.right - 28, card.bottom - 220}, ui.fontBody, colText, dtLeft|dtWordBreak|dtNoPrefix)
	if showDetails {
		dr := rect{card.left + 28, card.bottom - 206, card.right - 28, card.bottom - 92}
		drawPanel(hdc, dr, colPanelSoft, colBorder, 14)
		text(hdc, "Additional information", rect{dr.left + 16, dr.top + 10, dr.right - 16, dr.top + 34}, ui.fontSmall, colText, dtLeft|dtSingleLine|dtNoPrefix)
		logText := latestLogs(logs, 3)
		if logText == "" {
			logText = detail
		}
		if logText == "" {
			logText = "No details yet."
		}
		text(hdc, logText, rect{dr.left + 16, dr.top + 40, dr.right - 16, dr.bottom - 10}, ui.fontSmall, colTextMuted, dtLeft|dtWordBreak|dtNoPrefix)
	} else if detail != "" {
		dr := rect{card.left + 28, card.bottom - 176, card.right - 28, card.bottom - 112}
		drawPanel(hdc, dr, colPanelSoft, colBorder, 14)
		text(hdc, detail, rect{dr.left + 16, dr.top + 12, dr.right - 16, dr.bottom - 10}, ui.fontSmall, colTextMuted, dtLeft|dtWordBreak|dtNoPrefix)
	}
	by := int(card.bottom) - 64
	left := card.left + 28
	label := "Show details"
	if showDetails {
		label = "Hide details"
	}
	details := rect{left, int32(by), left + 150, int32(by + 44)}
	drawButton(hdc, details, label, ui.fontButton, false)
	ui.addZone(details, "details")
	left += 166
	cancel := rect{left, int32(by), left + 118, int32(by + 44)}
	drawButton(hdc, cancel, "Cancel", ui.fontButton, false)
	ui.addZone(cancel, "cancelUSB")
	left += 134
	cp := rect{left, int32(by), left + 188, int32(by + 44)}
	drawButton(hdc, cp, "Install CP210x", ui.fontButton, false)
	ui.addZone(cp, "driverCP210x")
	left += 204
	ch := rect{left, int32(by), left + 178, int32(by + 44)}
	drawButton(hdc, ch, "Install CH340", ui.fontButton, false)
	ui.addZone(ch, "driverCH340")
	try := rect{card.right - 192, int32(by), card.right - 28, int32(by + 44)}
	drawButton(hdc, try, "Try Again", ui.fontButton, true)
	ui.addZone(try, "retryUSB")
}

func (ui *NativeUI) paintCardScreen(hdc uintptr, w, h int, body, detail string, step, total, progress int, primary, secondary string, busy bool, canDetails bool, logs []string, showDetails bool) {
	card := rect{34, 112, int32(w - 34), int32(h - 78)}
	drawPanel(hdc, card, colPanel, colBorderSoft, 24)
	top := int(card.top) + 28
	if total > 0 {
		label := fmt.Sprintf("Step %d of %d", step, total)
		if step <= 0 {
			label = fmt.Sprintf("Step 1 of %d", total)
		}
		text(hdc, label, rect{card.left + 28, int32(top), card.right - 28, int32(top + 24)}, ui.fontSmall, colTextMuted, dtLeft|dtSingleLine|dtNoPrefix)
		ui.drawProgress(hdc, rect{card.left + 28, int32(top + 34), card.right - 28, int32(top + 48)}, progress)
		top += 76
	}
	bodyBottom := card.bottom - 150
	if showDetails && canDetails {
		bodyBottom = card.bottom - 286
	}
	if busy {
		drawSpinnerFallback(hdc, card.left+28, int32(top+4))
		text(hdc, body, rect{card.left + 70, int32(top), card.right - 28, bodyBottom}, ui.fontBody, colText, dtLeft|dtWordBreak|dtNoPrefix)
	} else {
		text(hdc, body, rect{card.left + 28, int32(top), card.right - 28, bodyBottom}, ui.fontBody, colText, dtLeft|dtWordBreak|dtNoPrefix)
	}
	if showDetails && canDetails {
		dr := rect{card.left + 28, card.bottom - 274, card.right - 28, card.bottom - 92}
		drawPanel(hdc, dr, colPanelSoft, colBorder, 14)
		text(hdc, "Additional information", rect{dr.left + 16, dr.top + 10, dr.right - 16, dr.top + 34}, ui.fontSmall, colText, dtLeft|dtSingleLine|dtNoPrefix)
		logText := latestLogs(logs, 3)
		if logText == "" {
			logText = "No details yet."
		}
		text(hdc, logText, rect{dr.left + 16, dr.top + 40, dr.right - 16, dr.bottom - 22}, ui.fontSmall, colTextMuted, dtLeft|dtWordBreak|dtNoPrefix)
	} else if detail != "" {
		dr := rect{card.left + 28, card.bottom - 138, card.right - 28, card.bottom - 86}
		drawPanel(hdc, dr, colPanelSoft, colBorder, 14)
		text(hdc, detail, rect{dr.left + 16, dr.top + 12, dr.right - 16, dr.bottom - 10}, ui.fontSmall, colTextMuted, dtLeft|dtWordBreak|dtNoPrefix)
	}
	by := int(card.bottom) - 64
	leftX := card.left + 28
	if canDetails {
		label := "Show details"
		if showDetails {
			label = "Hide details"
		}
		drBtn := rect{leftX, int32(by), leftX + 170, int32(by + 44)}
		drawButton(hdc, drBtn, label, ui.fontButton, false)
		ui.addZone(drBtn, "details")
		leftX += 186
		copyBtn := rect{leftX, int32(by), leftX + 150, int32(by + 44)}
		drawButton(hdc, copyBtn, "Copy log", ui.fontButton, false)
		ui.addZone(copyBtn, "copyLog")
		leftX += 166
	}
	if secondary != "" {
		sr := rect{leftX, int32(by), leftX + 220, int32(by + 44)}
		drawButton(hdc, sr, secondary, ui.fontButton, false)
		if secondary == "Back" {
			ui.addZone(sr, "back")
		} else {
			ui.addZone(sr, "openBackup")
		}
	}
	if primary != "" {
		pr := rect{card.right - 314, int32(by), card.right - 28, int32(by + 44)}
		if primary == "Back to start" {
			pr = rect{card.right - 230, int32(by), card.right - 28, int32(by + 44)}
		}
		drawButton(hdc, pr, primary, ui.fontButton, true)
		action := "continue"
		if primary == "I have done this — continue" {
			action = "start"
		}
		if primary == "Back to start" {
			action = "home"
		}
		ui.addZone(pr, action)
	}
}

func latestLogs(logs []string, max int) string {
	if max <= 0 || len(logs) == 0 {
		return ""
	}
	start := len(logs) - max
	if start < 0 {
		start = 0
	}
	return strings.Join(logs[start:], "\n")
}

func (ui *NativeUI) drawProgress(hdc uintptr, r rect, percent int) {
	if percent < 0 {
		percent = 0
	}
	if percent > 100 {
		percent = 100
	}
	drawPanel(hdc, r, colPanelSoft, colBorderSoft, 7)
	fillW := int(float64(r.right-r.left) * float64(percent) / 100.0)
	if fillW > 0 {
		rr := r
		rr.right = rr.left + int32(fillW)
		drawPanel(hdc, rr, colAccent, colAccent, 7)
	}
}

func (ui *NativeUI) addZone(r rect, action string) {
	ui.mu.Lock()
	ui.zones = append(ui.zones, clickZone{r: r, action: action})
	ui.mu.Unlock()
}

func fill(hdc uintptr, r rect, color uint32) {
	brush, _, _ := procCreateSolidBrush.Call(uintptr(color))
	procFillRect.Call(hdc, uintptr(unsafe.Pointer(&r)), brush)
	procDeleteObject.Call(brush)
}

func drawPanel(hdc uintptr, r rect, fillColor, borderColor uint32, radius int) {
	brush, _, _ := procCreateSolidBrush.Call(uintptr(fillColor))
	pen, _, _ := procCreatePen.Call(psSolid, 1, uintptr(borderColor))
	oldB, _, _ := procSelectObject.Call(hdc, brush)
	oldP, _, _ := procSelectObject.Call(hdc, pen)
	procRoundRect.Call(hdc, uintptr(r.left), uintptr(r.top), uintptr(r.right), uintptr(r.bottom), uintptr(radius), uintptr(radius))
	procSelectObject.Call(hdc, oldB)
	procSelectObject.Call(hdc, oldP)
	procDeleteObject.Call(brush)
	procDeleteObject.Call(pen)
}

func drawButton(hdc uintptr, r rect, label string, font uintptr, primary bool) {
	fillColor := colPanelSoft
	borderColor := colBorder
	textColor := colText
	if primary {
		fillColor = colAccent
		borderColor = colAccent
		textColor = colText
	}
	drawPanel(hdc, r, fillColor, borderColor, 18)
	text(hdc, label, r, font, textColor, dtCenter|dtVCenter|dtSingleLine|dtNoPrefix)
}

func drawSpinnerFallback(hdc uintptr, x, y int32) {
	// Static progress mark; avoids animation complexity and still gives a modern cue.
	drawPanel(hdc, rect{x, y, x + 28, y + 28}, colAccentDark, colAccent, 14)
	line(hdc, int(x+8), int(y+14), int(x+14), int(y+20), colAccent2, 3)
	line(hdc, int(x+14), int(y+20), int(x+22), int(y+8), colAccent2, 3)
}

func line(hdc uintptr, x1, y1, x2, y2 int, color uint32, width int) {
	pen, _, _ := procCreatePen.Call(psSolid, uintptr(width), uintptr(color))
	old, _, _ := procSelectObject.Call(hdc, pen)
	procMoveToEx.Call(hdc, uintptr(x1), uintptr(y1), 0)
	procLineTo.Call(hdc, uintptr(x2), uintptr(y2))
	procSelectObject.Call(hdc, old)
	procDeleteObject.Call(pen)
}

func text(hdc uintptr, s string, r rect, font uintptr, color uint32, flags uintptr) {
	old, _, _ := procSelectObject.Call(hdc, font)
	procSetTextColor.Call(hdc, uintptr(color))
	procSetBkMode.Call(hdc, transparent)
	p := utf16Ptr(s)
	procDrawTextW.Call(hdc, uintptr(unsafe.Pointer(p)), ^uintptr(0), uintptr(unsafe.Pointer(&r)), flags)
	procSelectObject.Call(hdc, old)
}

func createFont(height, weight int32) uintptr {
	h, _, _ := procCreateFontW.Call(uintptr(height), 0, 0, 0, uintptr(weight), 0, 0, 0, 1, 0, 0, 5, 0, uintptr(unsafe.Pointer(utf16Ptr("Segoe UI"))))
	return h
}

func rgb(r, g, b byte) uint32 { return uint32(r) | uint32(g)<<8 | uint32(b)<<16 }
func pointInRect(x, y int, r rect) bool {
	return int32(x) >= r.left && int32(x) <= r.right && int32(y) >= r.top && int32(y) <= r.bottom
}
func utf16Ptr(s string) *uint16 { p, _ := syscall.UTF16PtrFromString(s); return p }

func setClipboardText(hwnd uintptr, s string) error {
	r, _, err := procOpenClipboard.Call(hwnd)
	if r == 0 {
		return err
	}
	defer procCloseClipboard.Call()
	procEmptyClipboard.Call()
	data := append(syscall.StringToUTF16(s), 0)
	size := uintptr(len(data) * 2)
	hmem, _, err := procGlobalAlloc.Call(gmemMoveable, size)
	if hmem == 0 {
		return err
	}
	ptr, _, err := procGlobalLock.Call(hmem)
	if ptr == 0 {
		return err
	}
	dst := unsafe.Slice((*byte)(unsafe.Pointer(ptr)), int(size))
	src := unsafe.Slice((*byte)(unsafe.Pointer(&data[0])), int(size))
	copy(dst, src)
	procGlobalUnlock.Call(hmem)
	r, _, err = procSetClipboardData.Call(cfUnicodeText, hmem)
	if r == 0 {
		return err
	}
	return nil
}

func setTitleBarColors(hwnd uintptr) {
	if hwnd == 0 {
		return
	}
	enabled := int32(1)
	// 20 = DWMWA_USE_IMMERSIVE_DARK_MODE. Older Windows builds ignore failures.
	procDwmSetWindowAttribute.Call(hwnd, 20, uintptr(unsafe.Pointer(&enabled)), unsafe.Sizeof(enabled))
	caption := colTitleBar
	textColor := colText
	// 35/36 = DWMWA_CAPTION_COLOR / DWMWA_TEXT_COLOR on modern Windows.
	procDwmSetWindowAttribute.Call(hwnd, 35, uintptr(unsafe.Pointer(&caption)), unsafe.Sizeof(caption))
	procDwmSetWindowAttribute.Call(hwnd, 36, uintptr(unsafe.Pointer(&textColor)), unsafe.Sizeof(textColor))
}

// ---------------- Job flow ----------------

func (j *Job) ui() *NativeUI { return globalUI }

func (j *Job) set(step, total, progress int, title, body, detail string, wait bool) {
	j.addLog(title + " - " + oneLine(body))
	screen := screenRunning
	primary := ""
	if wait {
		screen = screenWait
		primary = "Continue"
	}
	j.ui().update(uiUpdate{screen: screen, title: title, subtitle: subtitleForMode(j.mode), body: body, detail: detail, step: step, totalSteps: total, progress: progress, primary: primary, mode: j.mode})
}

func (j *Job) addLog(s string) {
	line := time.Now().Format("2006-01-02 15:04:05") + "  " + s
	j.mu.Lock()
	j.logs = append(j.logs, line)
	j.mu.Unlock()
	if globalUI != nil {
		globalUI.mu.Lock()
		globalUI.logs = append(globalUI.logs, line)
		globalUI.mu.Unlock()
		globalUI.invalidate()
	}
}

func (j *Job) fail(err error) {
	msg := friendlyError(err.Error())
	j.addLog("FAILED: " + err.Error())
	j.writeLog("failed", err.Error())
	body := msg + "\n\nNo update is running now. You can go back to the start screen and try again."
	secondary := "Open folder"
	if j.backupPath != "" {
		secondary = "Open backup folder"
	}
	j.ui().update(uiUpdate{screen: screenError, title: "Could not finish", subtitle: subtitleForMode(j.mode), body: body, detail: "A support log was saved in Documents\\OpenTurbine\\SetupTool or next to the settings backup if one was created.", step: 0, totalSteps: 0, progress: 0, mode: j.mode, secondary: secondary, done: true, backupPath: j.backupPath})
}

func (j *Job) success(title, body string) {
	j.addLog("DONE: " + title)
	j.writeLog("done", body)
	j.ui().update(uiUpdate{screen: screenDone, title: title, subtitle: subtitleForMode(j.mode), body: body, detail: "Keep fuel disconnected until Hardware, Config, Calibration, and Sequence have been checked.", step: 0, totalSteps: 0, progress: 100, mode: j.mode, done: true, backupPath: j.backupPath})
}

func (j *Job) waitContinue() { <-j.continueCh }

func (j *Job) showDriverHelp(pkg *Package, reason string) string {
	body := "Board not found.\n\nTry this first:\n\n1. Use a USB data cable, not a charge-only cable.\n2. Plug the board directly into this computer.\n3. Hold BOOT on the board, then click Try Again.\n4. Close Arduino IDE, PlatformIO, Cura, serial monitors, or anything else that may use the COM port.\n\nIf it still does not work, install the USB driver for your board."
	detail := reason + "\n\nDriver buttons use installers from the downloaded OpenTurbine package. CP210x covers Silicon Labs USB serial boards. CH340 covers WCH CH340, CH341, and CH343 USB serial boards."
	j.addLog("Driver Help shown: " + reason)
	j.ui().update(uiUpdate{screen: screenDriverHelp, title: "Board not found", subtitle: subtitleForMode(j.mode), body: body, detail: detail, step: 0, totalSteps: 0, progress: 0, mode: j.mode, appendLog: []string{reason}})
	for {
		action := <-j.actionCh
		switch action {
		case "cp210x", "ch340":
			name := "CP210x"
			if action == "ch340" {
				name = "CH340/CH341/CH343"
			}
			if err := startDriverInstaller(pkg, action); err != nil {
				msg := name + " driver installer was not found or could not be opened: " + err.Error()
				j.addLog(msg)
				j.ui().update(uiUpdate{screen: screenDriverHelp, title: "Driver installer not found", subtitle: subtitleForMode(j.mode), body: body, detail: msg + "\n\nYou can still use Try Again if the driver is already installed.", mode: j.mode, appendLog: []string{msg}})
				continue
			}
			msg := name + " driver installer opened. Approve the Windows prompt if it appears. After installation, unplug and replug the board, then click Try Again."
			j.addLog(msg)
			j.ui().update(uiUpdate{screen: screenDriverHelp, title: "Driver installer opened", subtitle: subtitleForMode(j.mode), body: "Driver installer opened.\n\nApprove the Windows prompt if it appears. When the installer is finished:\n\n1. Unplug the board.\n2. Plug it in again.\n3. Hold BOOT if needed.\n4. Click Try Again.", detail: msg, mode: j.mode, appendLog: []string{msg}})
		case "retry", "cancel":
			return action
		}
	}
}

func (j *Job) cancelToHome(msg string) {
	j.addLog(msg)
	j.writeLog("cancelled", msg)
	j.ui().showHome()
}

func subtitleForMode(mode string) string {
	if mode == "update" {
		return "Existing board update"
	}
	if mode == "new" {
		return "New board setup"
	}
	return ""
}

func (j *Job) runNewBoard() {
	total := 5
	j.set(1, total, 5, "Preparing setup files", "Stay connected to your normal internet Wi‑Fi. The tool is preparing the recommended OpenTurbine setup files.", "For a clean new-board setup, the board will be erased and OpenTurbine will be installed from the recommended package.", false)
	pkg, err := j.app.ensurePackage()
	if err != nil {
		j.fail(err)
		return
	}

	j.set(2, total, 20, "Plug in the board", "Plug the ESP32 or ESP32‑S3 board into this computer with USB.\n\nIf it is not found, hold BOOT on the board and then click Continue.", "Use a USB data cable, not a charge-only cable.", true)
	j.waitContinue()

	esptool, err := findEsptool(pkg)
	if err != nil {
		j.fail(err)
		return
	}

	var target, port string
	for {
		ports := findSerialPorts()
		if len(ports) == 0 {
			action := j.showDriverHelp(pkg, "No serial COM port was found for the board.")
			if action == "cancel" {
				j.cancelToHome("New-board setup was cancelled.")
				return
			}
			continue
		}

		j.set(3, total, 35, "Detecting board", "The tool is looking for an ESP32 board over USB.", "This usually takes a few seconds. If it fails, hold BOOT and try again.", false)
		for _, p := range ports {
			t, err := detectTargetWithEsptool(esptool, p)
			if err == nil && t != "" {
				target, port = t, p
				break
			}
		}
		if target != "" {
			break
		}
		action := j.showDriverHelp(pkg, "A COM port was found, but the ESP32 bootloader did not answer. The port may be busy, the board may need BOOT held, or the wrong driver may be installed.")
		if action == "cancel" {
			j.cancelToHome("New-board setup was cancelled.")
			return
		}
	}
	j.addLog("Detected board on " + port + ": " + friendlyTarget(target))

	version := packageVersion(pkg)
	j.addLog("Using package " + version + " for " + friendlyTarget(target))
	j.set(4, total, 55, "Installing OpenTurbine "+version, "Detected "+friendlyTarget(target)+" on "+port+".\n\nDo not unplug USB or power. The board will be erased and OpenTurbine "+version+" will be installed.", "Package target: "+target+". This is the correct path for blank or new boards.", false)
	if err := flashUSB(esptool, port, target, pkg, j.addLog); err != nil {
		j.fail(err)
		return
	}

	j.success("OpenTurbine installed", "OpenTurbine was installed and verified successfully.\n\nThe board is restarting now. After restart, connect your computer or phone to the board Wi-Fi. The Wi-Fi name may be OpenTurbine, or it may be your own engine/project name. Windows may say 'No internet'; that is normal.\n\nThen open:\nhttp://192.168.4.1")
}

func (j *Job) runExistingUpdate() {
	total := 7
	j.set(1, total, 5, "Downloading update", "Stay connected to your normal internet Wi‑Fi. Do not connect to the board Wi‑Fi yet.\n\nThe tool is downloading the recommended OpenTurbine update first.", "The board Wi‑Fi often has no internet, so this step happens before switching Wi‑Fi.", false)
	pkg, err := j.app.ensurePackage()
	if err != nil {
		j.fail(err)
		return
	}

	j.set(2, total, 20, "Connect to board Wi‑Fi", "Now switch Wi‑Fi.\n\nConnect this computer to the OpenTurbine board Wi‑Fi. The Wi‑Fi name may be OpenTurbine, or it may be your own engine/project name. Windows may say 'No internet'; that is normal.\n\nAfter connecting, click Continue.", "The tool will look for the board at http://192.168.4.1.", true)
	j.waitContinue()

	j.set(2, total, 25, "Finding board", "The tool is looking for the board at http://192.168.4.1.", "Check that Windows is still connected to the board Wi‑Fi.", false)
	if err := waitForECU(25 * time.Second); err != nil {
		j.fail(errors.New("The board was not found. Check that this computer is connected to the board Wi‑Fi, then try again. The Wi‑Fi name may be OpenTurbine or your own engine/project name."))
		return
	}

	if err := checkSafeStatus(); err != nil {
		j.fail(err)
		return
	}

	j.set(3, total, 36, "Saving settings", "Before updating, the tool is saving your OpenTurbine settings.\n\nThis is done every time so your engine setup does not vanish if something goes wrong.", "The backup may contain the board Wi‑Fi password. Keep it private.", false)
	bpath, err := backupConfig()
	if err != nil {
		j.fail(errors.New("Settings backup failed. The update was not started. Reconnect to the board Wi‑Fi and try again."))
		return
	}
	j.backupPath = bpath
	j.addLog("Settings backup saved: " + bpath)

	target, err := chooseTargetForOTA(pkg)
	if err != nil {
		j.fail(errors.New("The tool could not choose the correct board update file automatically. Use a recommended update package with one board target, or update this board once over USB with a package that reports device_info."))
		return
	}
	if target != "" {
		j.addLog("Using update target: " + friendlyTarget(target))
	}
	version := packageVersion(pkg)
	j.addLog("Using package " + version + " for " + friendlyTarget(target))
	firmware, err := firmwarePath(pkg, target)
	if err != nil {
		j.fail(err)
		return
	}

	j.set(4, total, 50, "Updating OpenTurbine "+version, "Target: "+friendlyTarget(target)+".\n\nDo not unplug power. The board will restart after this step.", "Package target: "+target+". If Windows disconnects from the board Wi-Fi after restart, the tool will pause and tell you what to do.", false)
	if err := postMultipartFilesWithProgress(ecuBaseURL+"/update", []string{firmware}, 10*time.Minute, func(done, totalBytes int64) {
		if totalBytes > 0 {
			p := 50 + int((done*15)/totalBytes)
			j.ui().update(uiUpdate{screen: screenRunning, title: "Updating board software", subtitle: subtitleForMode(j.mode), body: fmt.Sprintf("Sending board software to the OpenTurbine board... %d%%", int((done*100)/totalBytes)), detail: "Do not unplug power. Keep this computer connected to the board Wi-Fi.", step: 4, totalSteps: total, progress: p, mode: j.mode})
		}
	}); err != nil {
		j.fail(fmt.Errorf("Board software update failed. Make sure the engine is in STANDBY and no actuator test is running. Details: %w", err))
		return
	}

	j.set(5, total, 68, "Check Wi‑Fi connection", "The board restarted. Windows may have switched away from the board Wi‑Fi.\n\nCheck that this computer is connected to the correct board Wi‑Fi again before continuing. The Wi‑Fi name may be OpenTurbine, or it may be your own engine/project name.\n\nThen click Continue.", "This check is important before dashboard files are sent to the board.", true)
	j.waitContinue()

	j.set(5, total, 72, "Finding board again", "The tool is checking that the board is back online at http://192.168.4.1.", "If this fails, reconnect to the board Wi‑Fi and run the update again.", false)
	if err := waitForECU(75 * time.Second); err != nil {
		j.fail(errors.New("The board software was uploaded, but the tool could not reconnect after restart. Check that this computer is connected to the board Wi‑Fi, then try again."))
		return
	}

	assets, err := webAssetPaths(pkg, target)
	if err != nil {
		j.fail(err)
		return
	}
	j.set(6, total, 82, "Updating dashboard", "Do not unplug power. The tool is updating the OpenTurbine dashboard files.", "Keep this computer connected to the board Wi‑Fi until this step finishes.", false)
	if err := postMultipartFilesWithProgress(ecuBaseURL+"/api/web_assets", assets, 10*time.Minute, func(done, totalBytes int64) {
		if totalBytes > 0 {
			p := 82 + int((done*10)/totalBytes)
			j.ui().update(uiUpdate{screen: screenRunning, title: "Updating dashboard", subtitle: subtitleForMode(j.mode), body: fmt.Sprintf("Sending dashboard files to the OpenTurbine board... %d%%", int((done*100)/totalBytes)), detail: "Keep this computer connected to the board Wi-Fi until this finishes.", step: 6, totalSteps: total, progress: p, mode: j.mode})
		}
	}); err != nil {
		j.fail(fmt.Errorf("Dashboard update failed. Check that this computer is still connected to the board Wi‑Fi, then try again. Details: %w", err))
		return
	}

	j.set(7, total, 94, "Final check", "The board may restart once more. Check the board Wi‑Fi again if Windows disconnects.", "The update is almost finished.", false)
	time.Sleep(6 * time.Second)
	_ = waitForECU(45 * time.Second)
	j.success("Update complete", "OpenTurbine was updated successfully.\n\nYour settings backup was saved here:\n"+bpath+"\n\nOpen http://192.168.4.1 and check the setup before using fuel.")
}

func (j *Job) writeLog(status, msg string) {
	dir := j.app.workDir
	if j.backupPath != "" {
		dir = filepath.Dir(j.backupPath)
	}
	_ = os.MkdirAll(dir, 0755)
	j.mu.Lock()
	defer j.mu.Unlock()
	var b strings.Builder
	b.WriteString("OpenTurbine Setup Tool " + appVersion + "\r\n")
	b.WriteString("Status: " + status + "\r\n")
	b.WriteString("Mode: " + j.mode + "\r\n")
	if j.backupPath != "" {
		b.WriteString("Backup: " + j.backupPath + "\r\n")
	}
	b.WriteString("Message: " + msg + "\r\n\r\n")
	for _, l := range j.logs {
		b.WriteString(l + "\r\n")
	}
	_ = os.WriteFile(filepath.Join(dir, "update_log.txt"), []byte(b.String()), 0644)
}

func friendlyError(s string) string {
	lower := strings.ToLower(s)
	switch {
	case strings.Contains(lower, "settings backup failed"):
		return "Settings backup failed.\n\nThe update was not started. Reconnect to the board Wi‑Fi and try again."
	case strings.Contains(lower, "board was not found") || strings.Contains(lower, "could not reconnect"):
		return "The board was not found.\n\nCheck that this computer is connected to the board Wi‑Fi. The Wi‑Fi name may be OpenTurbine or your own engine/project name. Windows may say 'No internet'; that is normal."
	case strings.Contains(lower, "no usb board"):
		return "No USB board was found.\n\nPlug the board into USB. If needed, hold BOOT on the board and try again. Use a USB data cable, not a charge-only cable."
	case strings.Contains(lower, "usb erase failed") ||
		strings.Contains(lower, "usb install failed") ||
		strings.Contains(lower, "failed to connect") ||
		strings.Contains(lower, "wrong boot mode") ||
		strings.Contains(lower, "timed out waiting") ||
		strings.Contains(lower, "no serial data received") ||
		strings.Contains(lower, "serial data stream stopped") ||
		strings.Contains(lower, "bootloader did not answer"):
		return "The board did not enter USB boot mode.\n\nHold BOOT on the ESP32 board, click Back to start, and try the new-board setup again. Keep BOOT held until the tool starts writing, then release it.\n\nAlso check that no serial monitor or other app is using the COM port."
	case strings.Contains(lower, "missing tools\\esptool.exe") ||
		strings.Contains(lower, "needs esptool.exe") ||
		strings.Contains(lower, "include tools\\esptool.exe"):
		return "USB setup files are incomplete.\n\nThe recommended OpenTurbine package must include tools\\esptool.exe, or esptool.exe must be placed next to this app."
	case strings.Contains(lower, "not in standby"):
		return "The board is not in STANDBY.\n\nStop the engine, make sure no actuator test is running, then update again."
	case strings.Contains(lower, "download"):
		return "The update could not be downloaded.\n\nStay connected to your normal internet Wi‑Fi and try again. If this is a test build, place OpenTurbine_Recommended.zip next to the app."
	default:
		return s
	}
}

func oneLine(s string) string { return strings.Join(strings.Fields(s), " ") }
func friendlyTarget(t string) string {
	if t == "esp32s3dev" {
		return "ESP32-S3"
	}
	if t == "esp32dev" {
		return "ESP32"
	}
	return t
}

func packageVersion(pkg *Package) string {
	if pkg == nil || strings.TrimSpace(pkg.Manifest.Version) == "" {
		return "from the recommended package"
	}
	return strings.TrimSpace(pkg.Manifest.Version)
}

// ---------------- Update / flashing backend ----------------

func (a *App) ensurePackage() (*Package, error) {
	return a.ensurePackageWithProgress(nil)
}

func (a *App) ensurePackageWithProgress(progress func(string, int)) (*Package, error) {
	exe, _ := os.Executable()
	base := "."
	if exe != "" {
		base = filepath.Dir(exe)
	}
	candidates := []string{
		filepath.Join(base, "OpenTurbine_Recommended.zip"),
		filepath.Join(base, "package", "OpenTurbine_Recommended.zip"),
		filepath.Join(a.workDir, "packages", "OpenTurbine_Recommended.zip"),
		filepath.Join(a.workDir, "OpenTurbine_Recommended.zip"),
	}
	for _, c := range candidates {
		if !fileExists(c) {
			continue
		}
		if progress != nil {
			progress("Checking downloaded OpenTurbine package.", 28)
		}
		pkg, err := loadPackageFromZip(c)
		if err == nil {
			if _, eerr := findEsptool(pkg); eerr == nil {
				return pkg, nil
			} else if strings.HasPrefix(c, a.workDir) {
				// Cached package is incomplete or obsolete. Delete it and try GitHub again.
				_ = os.Remove(c)
				continue
			} else {
				return nil, fmt.Errorf("the local OpenTurbine package is missing tools\\esptool.exe, needed for new-board USB setup: %w", eerr)
			}
		}
		if strings.HasPrefix(c, a.workDir) {
			_ = os.Remove(c)
			continue
		}
		return nil, fmt.Errorf("the local OpenTurbine package could not be opened: %w", err)
	}
	if fileExists(filepath.Join(base, "package", "manifest.json")) {
		if progress != nil {
			progress("Checking local OpenTurbine package.", 28)
		}
		pkg, err := loadPackageFromDir(filepath.Join(base, "package"))
		if err != nil {
			return nil, err
		}
		if _, eerr := findEsptool(pkg); eerr != nil {
			return nil, fmt.Errorf("the local OpenTurbine package is missing tools\\esptool.exe, needed for new-board USB setup: %w", eerr)
		}
		return pkg, nil
	}
	url := strings.TrimSpace(a.config.PackageURL)
	if url == "" {
		url = defaultPackageURL
	}
	dst := filepath.Join(a.workDir, "packages", "OpenTurbine_Recommended.zip")
	if err := os.MkdirAll(filepath.Dir(dst), 0755); err != nil {
		return nil, err
	}
	if progress != nil {
		progress("Downloading the recommended OpenTurbine files from GitHub.", 34)
	}
	if err := downloadFileWithProgress(url, dst, func(done, total int64) {
		if progress == nil {
			return
		}
		if total > 0 {
			pct := 34 + int((done*42)/total)
			progress(fmt.Sprintf("Downloading OpenTurbine setup files... %d%%", int((done*100)/total)), pct)
		} else {
			progress("Downloading OpenTurbine setup files...", 42)
		}
	}); err != nil {
		return nil, fmt.Errorf("could not download OpenTurbine_Recommended.zip from GitHub Releases. Publish a release asset with that exact name, then try again. Details: %w", err)
	}
	if progress != nil {
		progress("Checking downloaded OpenTurbine package checksum.", 78)
	}
	if err := verifyOptionalRemoteSHA256(url+".sha256", dst); err != nil {
		_ = os.Remove(dst)
		return nil, err
	}
	if progress != nil {
		progress("Checking downloaded OpenTurbine package.", 80)
	}
	pkg, err := loadPackageFromZip(dst)
	if err != nil {
		_ = os.Remove(dst)
		return nil, fmt.Errorf("the downloaded OpenTurbine package could not be opened: %w", err)
	}
	if _, eerr := findEsptool(pkg); eerr != nil {
		return nil, fmt.Errorf("the downloaded OpenTurbine package is missing tools\\esptool.exe, needed for new-board USB setup: %w", eerr)
	}
	if progress != nil {
		progress("OpenTurbine setup files are ready.", 94)
	}
	return pkg, nil
}

func verifyOptionalRemoteSHA256(shaURL, path string) error {
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, "GET", shaURL, nil)
	if err != nil {
		return nil
	}
	req.Header.Set("User-Agent", "OpenTurbineSetupTool/"+appVersion)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return nil // optional file; HTTPS download still works without it
	}
	defer resp.Body.Close()
	if resp.StatusCode == http.StatusNotFound {
		return nil
	}
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil
	}
	body, err := io.ReadAll(io.LimitReader(resp.Body, 4096))
	if err != nil {
		return nil
	}
	re := regexp.MustCompile(`(?i)[a-f0-9]{64}`)
	expected := re.FindString(string(body))
	if expected == "" {
		return fmt.Errorf("downloaded checksum file did not contain a SHA-256 value")
	}
	return verifySHA256(path, expected)
}

func downloadFile(url, dst string) error {
	return downloadFileWithProgress(url, dst, nil)
}

func downloadFileWithProgress(url, dst string, progress func(done, total int64)) error {
	if !strings.HasPrefix(strings.ToLower(url), "http") {
		return fmt.Errorf("bad package URL")
	}
	ctx, cancel := context.WithTimeout(context.Background(), 4*time.Minute)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		return err
	}
	req.Header.Set("User-Agent", "OpenTurbineSetupTool/"+appVersion)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("download returned %s", resp.Status)
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0755); err != nil {
		return err
	}
	tmp := dst + ".tmp"
	f, err := os.Create(tmp)
	if err != nil {
		return err
	}
	_, copyErr := copyWithProgress(f, resp.Body, resp.ContentLength, progress)
	closeErr := f.Close()
	if copyErr != nil {
		_ = os.Remove(tmp)
		return copyErr
	}
	if closeErr != nil {
		_ = os.Remove(tmp)
		return closeErr
	}
	return os.Rename(tmp, dst)
}

func copyWithProgress(dst io.Writer, src io.Reader, total int64, progress func(done, total int64)) (int64, error) {
	if progress == nil {
		return io.Copy(dst, src)
	}
	buf := make([]byte, 64*1024)
	var done int64
	last := time.Now().Add(-time.Second)
	for {
		n, rerr := src.Read(buf)
		if n > 0 {
			wn, werr := dst.Write(buf[:n])
			done += int64(wn)
			if time.Since(last) > 250*time.Millisecond || (total > 0 && done >= total) {
				progress(done, total)
				last = time.Now()
			}
			if werr != nil {
				return done, werr
			}
			if wn != n {
				return done, io.ErrShortWrite
			}
		}
		if rerr == io.EOF {
			progress(done, total)
			return done, nil
		}
		if rerr != nil {
			return done, rerr
		}
	}
}

func loadPackageFromZip(zipPath string) (*Package, error) {
	root := filepath.Join(os.TempDir(), "openturbine_setup_pkg_"+fmt.Sprintf("%d", time.Now().UnixNano()))
	if err := unzip(zipPath, root); err != nil {
		return nil, err
	}
	return loadPackageFromDir(root)
}

func loadPackageFromDir(root string) (*Package, error) {
	data, err := os.ReadFile(filepath.Join(root, "manifest.json"))
	if err != nil {
		return nil, fmt.Errorf("setup package is missing manifest.json")
	}
	var m Manifest
	if err := json.Unmarshal(data, &m); err != nil {
		return nil, fmt.Errorf("setup package manifest is not valid JSON: %w", err)
	}
	if strings.TrimSpace(m.Project) != "" && !strings.EqualFold(strings.TrimSpace(m.Project), "OpenTurbine") {
		return nil, fmt.Errorf("this setup package is not for OpenTurbine")
	}
	return &Package{Root: root, Manifest: m}, nil
}

func unzip(src, dst string) error {
	r, err := zip.OpenReader(src)
	if err != nil {
		return err
	}
	defer r.Close()
	for _, f := range r.File {
		clean := filepath.Clean(f.Name)
		if strings.HasPrefix(clean, "..") || filepath.IsAbs(clean) {
			return fmt.Errorf("unsafe package path: %s", f.Name)
		}
		p := filepath.Join(dst, clean)
		if f.FileInfo().IsDir() {
			if err := os.MkdirAll(p, 0755); err != nil {
				return err
			}
			continue
		}
		if err := os.MkdirAll(filepath.Dir(p), 0755); err != nil {
			return err
		}
		rc, err := f.Open()
		if err != nil {
			return err
		}
		out, err := os.Create(p)
		if err != nil {
			rc.Close()
			return err
		}
		_, copyErr := io.Copy(out, rc)
		closeErr := out.Close()
		rc.Close()
		if copyErr != nil {
			return copyErr
		}
		if closeErr != nil {
			return closeErr
		}
	}
	return nil
}

func startDriverInstaller(pkg *Package, kind string) error {
	path, err := findDriverInstaller(pkg, kind)
	if err != nil {
		return err
	}
	verb := "runas"
	// Some vendor installers carry their own elevation manifest. runas gives a clear UAC prompt when needed.
	ret, _, _ := procShellExecuteW.Call(0, uintptr(unsafe.Pointer(utf16Ptr(verb))), uintptr(unsafe.Pointer(utf16Ptr(path))), 0, 0, 1)
	if ret <= 32 {
		return fmt.Errorf("Windows could not open %s", filepath.Base(path))
	}
	return nil
}

func findDriverInstaller(pkg *Package, kind string) (string, error) {
	if pkg == nil {
		return "", fmt.Errorf("setup package is not loaded")
	}
	kind = strings.ToLower(kind)
	var roots []string
	switch kind {
	case "cp210x":
		roots = []string{filepath.Join(pkg.Root, "drivers", "cp210x"), filepath.Join(pkg.Root, "drivers", "CP210x")}
	case "ch340":
		roots = []string{filepath.Join(pkg.Root, "drivers", "ch340"), filepath.Join(pkg.Root, "drivers", "CH340"), filepath.Join(pkg.Root, "drivers", "ch341")}
	default:
		return "", fmt.Errorf("unknown driver type")
	}
	preferred := []string{}
	if kind == "cp210x" {
		preferred = []string{"CP210xVCPInstaller_x64.exe", "CP210xVCPInstaller_x86.exe", "CP210xVCPInstaller.exe"}
	} else {
		preferred = []string{"CH341SER.EXE", "CH340SER.EXE", "SETUP.EXE", "DRVSETUP64.exe"}
	}
	for _, root := range roots {
		for _, name := range preferred {
			p := filepath.Join(root, name)
			if fileExists(p) {
				return p, nil
			}
		}
	}
	for _, root := range roots {
		if !dirExists(root) {
			continue
		}
		var found string
		_ = filepath.WalkDir(root, func(path string, d os.DirEntry, err error) error {
			if err != nil || d.IsDir() || found != "" {
				return nil
			}
			if strings.EqualFold(filepath.Ext(path), ".exe") {
				found = path
			}
			return nil
		})
		if found != "" {
			return found, nil
		}
	}
	if kind == "cp210x" {
		return "", fmt.Errorf("expected drivers\\cp210x\\CP210xVCPInstaller_x64.exe inside OpenTurbine_Recommended.zip")
	}
	return "", fmt.Errorf("expected drivers\\ch340\\CH341SER.EXE inside OpenTurbine_Recommended.zip")
}

func findEsptool(pkg *Package) (string, error) {
	exe, _ := os.Executable()
	base := "."
	if exe != "" {
		base = filepath.Dir(exe)
	}
	names := []string{
		filepath.Join(base, "tools", "esptool.exe"),
		filepath.Join(base, "esptool.exe"),
	}
	if pkg != nil {
		names = append(names,
			filepath.Join(pkg.Root, "tools", "esptool.exe"),
			filepath.Join(pkg.Root, "esptool.exe"),
		)
	}
	for _, n := range names {
		if fileExists(n) {
			return n, nil
		}
	}
	return "", errors.New("USB install needs esptool.exe. The recommended OpenTurbine package should include tools\\esptool.exe, or esptool.exe can be placed next to this app.")
}

func findSerialPorts() []string {
	var ports []string
	out, err := exec.Command("reg", "query", `HKLM\HARDWARE\DEVICEMAP\SERIALCOMM`).CombinedOutput()
	if err == nil {
		re := regexp.MustCompile(`COM\d+`)
		ports = append(ports, re.FindAllString(string(out), -1)...)
	}
	seen := map[string]bool{}
	var unique []string
	for _, p := range ports {
		if !seen[p] {
			seen[p] = true
			unique = append(unique, p)
		}
	}
	sort.Strings(unique)
	return unique
}

func prepareEsptoolCommand(cmd *exec.Cmd) {
	cmd.Env = append(os.Environ(),
		"PYTHONIOENCODING=utf-8",
		"PYTHONUTF8=1",
		"NO_COLOR=1",
	)
	cmd.SysProcAttr = &syscall.SysProcAttr{
		HideWindow:    true,
		CreationFlags: createNoWindow,
	}
}

func detectTargetWithEsptool(esptool, port string) (string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 25*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, esptool, "--port", port, "chip_id")
	prepareEsptoolCommand(cmd)
	out, err := cmd.CombinedOutput()
	s := strings.ToLower(string(out))
	if strings.Contains(s, "esp32-s3") || strings.Contains(s, "esp32s3") {
		return "esp32s3dev", nil
	}
	if strings.Contains(s, "esp32") {
		return "esp32dev", nil
	}
	return "", err
}

func flashUSB(esptool, port, target string, pkg *Package, logf func(string)) error {
	t, ok := pkg.Manifest.Targets[target]
	if !ok {
		return fmt.Errorf("setup package does not contain files for this board")
	}
	if len(t.USBFlash) == 0 {
		return fmt.Errorf("setup package does not contain USB install instructions")
	}

	logf("Erasing board for clean new-board setup")
	eraseCtx, eraseCancel := context.WithTimeout(context.Background(), 4*time.Minute)
	eraseCmd := exec.CommandContext(eraseCtx, esptool, "--port", port, "erase_flash")
	prepareEsptoolCommand(eraseCmd)
	eraseOut, eraseErr := eraseCmd.CombinedOutput()
	eraseCancel()
	if eraseErr != nil {
		return fmt.Errorf("USB erase failed. Hold BOOT on the board and try again. Details: %s", strings.TrimSpace(string(eraseOut)))
	}

	args := []string{"--port", port, "--baud", "921600", "write-flash", "-z"}
	for _, e := range t.USBFlash {
		p, err := packageFile(pkg, target, e.File)
		if err != nil {
			return err
		}
		if e.SHA256 != "" {
			if err := verifySHA256(p, e.SHA256); err != nil {
				return err
			}
		}
		args = append(args, e.Address, p)
	}
	logf("Writing board software over USB")
	ctx, cancel := context.WithTimeout(context.Background(), 12*time.Minute)
	defer cancel()
	cmd := exec.CommandContext(ctx, esptool, args...)
	prepareEsptoolCommand(cmd)
	out, err := cmd.CombinedOutput()
	if err != nil {
		return fmt.Errorf("USB install failed. Hold BOOT on the board and try again. Details: %s", strings.TrimSpace(string(out)))
	}
	return nil
}

func packageFile(pkg *Package, target, name string) (string, error) {
	if strings.TrimSpace(name) == "" {
		return "", fmt.Errorf("setup package has a missing file name")
	}
	tries := []string{}
	if target != "" {
		tries = append(tries, filepath.Join(pkg.Root, target, filepath.FromSlash(name)))
	}
	tries = append(tries, filepath.Join(pkg.Root, filepath.FromSlash(name)))
	for _, p := range tries {
		if fileExists(p) {
			return p, nil
		}
	}
	return "", fmt.Errorf("setup package is missing %s", name)
}

func verifySHA256(path, expected string) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	h := sha256.New()
	if _, err := io.Copy(h, f); err != nil {
		return err
	}
	got := hex.EncodeToString(h.Sum(nil))
	expected = strings.ToLower(strings.TrimSpace(expected))
	if got != expected {
		return fmt.Errorf("setup package checksum failed for %s", filepath.Base(path))
	}
	return nil
}

func chooseTargetForOTA(pkg *Package) (string, error) {
	target, err := detectTargetFromDeviceInfo()
	if err == nil && target != "" {
		if _, ok := pkg.Manifest.Targets[target]; ok {
			return target, nil
		}
	}
	if strings.TrimSpace(pkg.Manifest.FirmwareOTA) != "" {
		return "", nil
	}
	if len(pkg.Manifest.Targets) == 1 {
		for k := range pkg.Manifest.Targets {
			return k, nil
		}
	}
	return "", errors.New("target not known")
}

func detectTargetFromDeviceInfo() (string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 4*time.Second)
	defer cancel()
	req, _ := http.NewRequestWithContext(ctx, "GET", ecuBaseURL+"/api/device_info", nil)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return "", fmt.Errorf("device_info returned %s", resp.Status)
	}
	var v struct {
		Target string `json:"target"`
		Chip   string `json:"chip"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&v); err != nil {
		return "", err
	}
	t := strings.ToLower(v.Target)
	if t == "esp32dev" || t == "esp32s3dev" {
		return t, nil
	}
	chip := strings.ToLower(v.Chip)
	if strings.Contains(chip, "s3") {
		return "esp32s3dev", nil
	}
	if strings.Contains(chip, "esp32") {
		return "esp32dev", nil
	}
	return "", fmt.Errorf("unknown target")
}

func firmwarePath(pkg *Package, target string) (string, error) {
	if target != "" {
		if t, ok := pkg.Manifest.Targets[target]; ok && t.FirmwareOTA != "" {
			return packageFile(pkg, target, t.FirmwareOTA)
		}
	}
	if pkg.Manifest.FirmwareOTA != "" {
		return packageFile(pkg, "", pkg.Manifest.FirmwareOTA)
	}
	if target != "" {
		if p, err := packageFile(pkg, target, "firmware.bin"); err == nil {
			return p, nil
		}
	}
	if p, err := packageFile(pkg, "", "firmware.bin"); err == nil {
		return p, nil
	}
	return "", fmt.Errorf("setup package is missing the board software file")
}

func webAssetPaths(pkg *Package, target string) ([]string, error) {
	var root string
	if target != "" {
		if t, ok := pkg.Manifest.Targets[target]; ok && t.WebAssets != "" {
			p := filepath.Join(pkg.Root, target, filepath.FromSlash(t.WebAssets))
			if fileExists(p) {
				return extractAssetsZipIfNeeded(p)
			}
			if dirExists(p) {
				root = p
			}
			if root == "" {
				p = filepath.Join(pkg.Root, filepath.FromSlash(t.WebAssets))
				if fileExists(p) {
					return extractAssetsZipIfNeeded(p)
				}
				if dirExists(p) {
					root = p
				}
			}
		}
	}
	if root == "" && pkg.Manifest.WebAssets != "" {
		p := filepath.Join(pkg.Root, filepath.FromSlash(pkg.Manifest.WebAssets))
		if fileExists(p) {
			return extractAssetsZipIfNeeded(p)
		}
		if dirExists(p) {
			root = p
		}
	}
	if root == "" && target != "" {
		root = filepath.Join(pkg.Root, target)
	}
	if root == "" || !dirExists(root) {
		root = pkg.Root
	}
	var paths []string
	for _, name := range webAssets {
		p := filepath.Join(root, name)
		if !fileExists(p) {
			return nil, fmt.Errorf("setup package is missing dashboard file %s", name)
		}
		paths = append(paths, p)
	}
	return paths, nil
}

func extractAssetsZipIfNeeded(zipPath string) ([]string, error) {
	if !strings.HasSuffix(strings.ToLower(zipPath), ".zip") {
		return nil, fmt.Errorf("dashboard files entry is not a folder or .zip")
	}
	root := filepath.Join(os.TempDir(), "openturbine_assets_"+fmt.Sprintf("%d", time.Now().UnixNano()))
	if err := unzip(zipPath, root); err != nil {
		return nil, err
	}
	var paths []string
	for _, name := range webAssets {
		p := filepath.Join(root, name)
		if !fileExists(p) {
			return nil, fmt.Errorf("dashboard package is missing %s", name)
		}
		paths = append(paths, p)
	}
	return paths, nil
}

func waitForECU(timeout time.Duration) error {
	deadline := time.Now().Add(timeout)
	var last error
	for time.Now().Before(deadline) {
		ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
		req, _ := http.NewRequestWithContext(ctx, "GET", ecuBaseURL+"/", nil)
		resp, err := http.DefaultClient.Do(req)
		cancel()
		if err == nil {
			io.Copy(io.Discard, resp.Body)
			resp.Body.Close()
			if resp.StatusCode >= 200 && resp.StatusCode < 600 {
				return nil
			}
		} else {
			last = err
		}
		time.Sleep(2 * time.Second)
	}
	if last != nil {
		return last
	}
	return fmt.Errorf("board did not respond")
}

func checkSafeStatus() error {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	req, _ := http.NewRequestWithContext(ctx, "GET", ecuBaseURL+"/api/status", nil)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return nil
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return nil
	}
	var s struct {
		Mode         string `json:"mode"`
		Locked       bool   `json:"locked"`
		ProfileMatch bool   `json:"profile_match"`
	}
	if err := json.NewDecoder(resp.Body).Decode(&s); err != nil {
		return nil
	}
	mode := strings.ToUpper(strings.TrimSpace(s.Mode))
	if mode != "STANDBY" && mode != "FAULT" {
		return fmt.Errorf("The board is not in STANDBY. Stop the engine and wait until it is in STANDBY before updating.")
	}
	return nil
}

func backupConfig() (string, error) {
	ctx, cancel := context.WithTimeout(context.Background(), 25*time.Second)
	defer cancel()
	req, _ := http.NewRequestWithContext(ctx, "GET", ecuBaseURL+"/api/ecu_config", nil)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return "", fmt.Errorf("backup returned %s", resp.Status)
	}
	dir := filepath.Join(backupDir(), time.Now().Format("2006-01-02_15-04-05"))
	if err := os.MkdirAll(dir, 0755); err != nil {
		return "", err
	}
	p := filepath.Join(dir, "ecu_config.json")
	f, err := os.Create(p)
	if err != nil {
		return "", err
	}
	_, copyErr := io.Copy(f, resp.Body)
	closeErr := f.Close()
	if copyErr != nil {
		return "", copyErr
	}
	if closeErr != nil {
		return "", closeErr
	}
	note := "This settings backup may contain the OpenTurbine board Wi-Fi password. Keep it private.\r\n"
	_ = os.WriteFile(filepath.Join(dir, "README.txt"), []byte(note), 0644)
	return p, nil
}

func postMultipartFilesWithProgress(url string, paths []string, timeout time.Duration, progress func(done, total int64)) error {
	var body bytes.Buffer
	mw := multipart.NewWriter(&body)
	for _, path := range paths {
		f, err := os.Open(path)
		if err != nil {
			return err
		}
		fw, err := mw.CreateFormFile("file", filepath.Base(path))
		if err != nil {
			f.Close()
			return err
		}
		_, copyErr := io.Copy(fw, f)
		f.Close()
		if copyErr != nil {
			return copyErr
		}
	}
	if err := mw.Close(); err != nil {
		return err
	}
	client := &http.Client{Timeout: timeout}
	reader := &uploadProgressReader{r: bytes.NewReader(body.Bytes()), total: int64(body.Len()), progress: progress, last: time.Now().Add(-time.Second)}
	req, err := http.NewRequest("POST", url, reader)
	if err != nil {
		return err
	}
	req.ContentLength = int64(body.Len())
	req.Header.Set("Content-Type", mw.FormDataContentType())
	resp, err := client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	respBody, _ := io.ReadAll(io.LimitReader(resp.Body, 8192))
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		return fmt.Errorf("board returned %s: %s", resp.Status, strings.TrimSpace(string(respBody)))
	}
	return nil
}

type uploadProgressReader struct {
	r        *bytes.Reader
	total    int64
	done     int64
	progress func(done, total int64)
	last     time.Time
}

func (r *uploadProgressReader) Read(p []byte) (int, error) {
	n, err := r.r.Read(p)
	if n > 0 {
		r.done += int64(n)
		if r.progress != nil && (time.Since(r.last) > 250*time.Millisecond || r.done >= r.total) {
			r.progress(r.done, r.total)
			r.last = time.Now()
		}
	}
	return n, err
}

func fileExists(p string) bool { st, err := os.Stat(p); return err == nil && !st.IsDir() }
func dirExists(p string) bool  { st, err := os.Stat(p); return err == nil && st.IsDir() }

func setupToolDataDir() string {
	if p := os.Getenv("LOCALAPPDATA"); p != "" {
		return filepath.Join(p, "OpenTurbine", "SetupTool")
	}
	return filepath.Join(userDocumentsDir(), "OpenTurbine", "SetupTool")
}

func userDocumentsDir() string {
	if p := os.Getenv("USERPROFILE"); p != "" {
		return filepath.Join(p, "Documents")
	}
	if p := os.Getenv("HOME"); p != "" {
		return filepath.Join(p, "Documents")
	}
	return "."
}
func backupDir() string { return filepath.Join(userDocumentsDir(), "OpenTurbine", "Backups") }
