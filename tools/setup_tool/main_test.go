//go:build windows

package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestParseDetectedBoardRejectsC3(t *testing.T) {
	board, err := parseDetectedBoard("COM7", "Chip is ESP32-C3 (revision v0.4)", nil)
	if err != nil || board.Target != "" || board.Chip != "ESP32-C3" {
		t.Fatalf("C3 must be identified but unsupported: board=%+v err=%v", board, err)
	}
}

func TestParseDetectedBoardSupportedFamilies(t *testing.T) {
	tests := []struct {
		output, target string
	}{
		{"Chip is ESP32-S3", "esp32s3dev"},
		{"Chip is ESP32-D0WDQ6", "esp32dev"},
	}
	for _, tc := range tests {
		board, err := parseDetectedBoard("COM4", tc.output, nil)
		if err != nil || board.Target != tc.target {
			t.Fatalf("output %q: board=%+v err=%v", tc.output, board, err)
		}
	}
}

func TestEsptoolProgressWriter(t *testing.T) {
	var got []int
	w := &esptoolProgressWriter{progress: func(percent int) { got = append(got, percent) }}
	_, _ = w.Write([]byte("Writing at 0x00010000... (12 %"))
	_, _ = w.Write([]byte(")"))
	_, _ = w.Write([]byte("\rWriting at 0x00020000... (73 %)"))
	if len(got) != 2 || got[0] != 12 || got[1] != 73 {
		t.Fatalf("unexpected progress callbacks: %v", got)
	}
}

func TestEsptoolV5AndMultiFileProgress(t *testing.T) {
	var got []int
	w := &esptoolProgressWriter{
		segmentSizes: []int64{100, 900},
		progress:     func(percent int) { got = append(got, percent) },
	}
	_, _ = w.Write([]byte("Writing at 0x00001000 [blocks] 100.0% 100/100 bytes...\n"))
	_, _ = w.Write([]byte("Writing at 0x00010000 [blocks]  50.0% 450/900 bytes..."))
	if len(got) < 2 || got[0] != 10 || got[len(got)-1] != 55 {
		t.Fatalf("unexpected aggregate progress: %v", got)
	}
}

func TestPrimaryButtonActions(t *testing.T) {
	tests := map[string]string{
		cleanSafetyButtonLabel:  "start",
		updateSafetyButtonLabel: "start",
		"Back to start":         "home",
		"Continue":              "continue",
	}
	for label, want := range tests {
		if got := primaryButtonAction(label); got != want {
			t.Fatalf("label %q: got action %q, want %q", label, got, want)
		}
	}
}

func TestTargetFromIdentity(t *testing.T) {
	tests := []struct{ target, chip, want string }{
		{"esp32dev", "", "esp32dev"},
		{"esp32s3dev", "", "esp32s3dev"},
		{"", "ESP32-S3", "esp32s3dev"},
		{"", "ESP32-D0WDQ6", "esp32dev"},
	}
	for _, tc := range tests {
		got, err := targetFromIdentity(tc.target, tc.chip)
		if err != nil || got != tc.want {
			t.Fatalf("target=%q chip=%q: got %q err=%v, want %q", tc.target, tc.chip, got, err, tc.want)
		}
	}
	if _, err := targetFromIdentity("", "ESP32-C3"); err == nil {
		t.Fatal("unsupported ESP32-C3 identity must not select classic ESP32 firmware")
	}
}

func TestFindDriverINFRootAcceptsNestedWCHPackages(t *testing.T) {
	root := t.TempDir()
	writeTestFile(t, filepath.Join(root, "drivers", "ch340", "ch341ser", "CH341SER.INF"), "inf")
	writeTestFile(t, filepath.Join(root, "drivers", "ch340", "ch343ser", "CH343SER.INF"), "inf")

	got, err := findDriverINFRoot(&Package{Root: root}, "ch340")
	want := filepath.Join(root, "drivers", "ch340")
	if err != nil || got != want {
		t.Fatalf("got %q err=%v, want %q", got, err, want)
	}
}

func TestFindCP210xINFRootFindsNestedUniversalDriver(t *testing.T) {
	root := t.TempDir()
	driverRoot := filepath.Join(root, "drivers", "cp210x", "Universal")
	writeTestFile(t, filepath.Join(driverRoot, "silabser.inf"), "inf")
	writeTestFile(t, filepath.Join(driverRoot, "silabser.cat"), "cat")

	got := findCP210xINFRoot(&Package{Root: root})
	if got != driverRoot {
		t.Fatalf("got %q, want %q", got, driverRoot)
	}
}

func TestPrepareBundledCP210xDriverCopiesToWorkDir(t *testing.T) {
	pkgRoot := t.TempDir()
	workDir := filepath.Join(t.TempDir(), "work")
	srcRoot := filepath.Join(pkgRoot, "drivers", "cp210x")
	writeTestFile(t, filepath.Join(srcRoot, "silabser.inf"), "inf")
	writeTestFile(t, filepath.Join(srcRoot, "silabser.cat"), "cat")
	writeTestFile(t, filepath.Join(srcRoot, "x64", "silabser.sys"), "sys")

	if err := prepareBundledCP210xDriver(&App{workDir: workDir}, &Package{Root: pkgRoot}); err != nil {
		t.Fatalf("prepareBundledCP210xDriver failed: %v", err)
	}

	dstRoot := filepath.Join(workDir, "drivers", "cp210x-universal-11.5.0")
	if !fileExists(filepath.Join(dstRoot, "silabser.inf")) || !fileExists(filepath.Join(dstRoot, "x64", "silabser.sys")) {
		t.Fatalf("bundled CP210x driver was not copied to %s", dstRoot)
	}
}

func writeTestFile(t *testing.T, path, content string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte(content), 0644); err != nil {
		t.Fatal(err)
	}
}
