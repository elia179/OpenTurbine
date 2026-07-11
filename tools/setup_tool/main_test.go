//go:build windows

package main

import "testing"

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
