package main

import (
	"os"
	"path/filepath"
	"regexp"
	"strings"
)

var cssVarPattern = regexp.MustCompile(`--([a-z0-9-]+)\s*:\s*(#[0-9a-fA-F]{6})\s*;`)

func loadThemeVars() map[string]string {
	vars := map[string]string{
		"surface":       "#000000",
		"panel-surface": "#000000",
		"panel-edge":    "#2F4B7A",
		"panel-glow":    "#8AB4FF",
		"ink":           "#EAF1FF",
		"muted":         "#95A9CD",
		"accent":        "#93B7FF",
		"cyan":          "#BFD3FF",
		"rose":          "#AAC3F4",
		"selected-bg":   "#9EC1FF",
		"selected-fg":   "#000000",
	}

	var cssData []byte
	candidates := []string{"theme.css"}
	if exePath, err := os.Executable(); err == nil {
		candidates = append(candidates, filepath.Join(filepath.Dir(exePath), "theme.css"))
	}
	if home, err := os.UserHomeDir(); err == nil {
		candidates = append(candidates, filepath.Join(home, ".blob", "theme.css"))
	}
	for _, candidate := range candidates {
		if data, err := os.ReadFile(candidate); err == nil {
			cssData = data
			break
		}
	}
	if len(cssData) == 0 {
		return vars
	}

	matches := cssVarPattern.FindAllStringSubmatch(string(cssData), -1)
	for _, m := range matches {
		if len(m) < 3 {
			continue
		}
		key := strings.ToLower(strings.TrimSpace(m[1]))
		val := strings.TrimSpace(m[2])
		vars[key] = val
	}
	// Always enforce OLED black backgrounds.
	vars["surface"] = "#000000"
	vars["panel-surface"] = "#000000"
	return vars
}
