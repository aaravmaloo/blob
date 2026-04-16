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
		"surface":       "#0d0d12",
		"panel-surface": "#13131f",
		"panel-edge":    "#2d2d44",
		"panel-glow":    "#7c3aed",
		"ink":           "#e2e8f0",
		"muted":         "#64748b",
		"accent":        "#a78bfa",
		"cyan":          "#22d3ee",
		"rose":          "#fb7185",
		"selected-bg":   "#7c3aed",
		"selected-fg":   "#ffffff",
		"gold":          "#fbbf24",
		"green":         "#4ade80",
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
	// Enforce dark theme base.
	vars["surface"] = "#0d0d12"
	vars["panel-surface"] = "#13131f"
	return vars
}
