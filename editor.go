package main

import "strings"

type editorMode string

const (
	editorModeInsert editorMode = "insert"
	editorModeNormal editorMode = "normal"
)

type editorState struct {
	lines       []string
	cursorRow   int
	cursorCol   int
	scrollRow   int
	mode        editorMode
	vimEnabled  bool
	dirty       bool
	lastEnterAt int64
}

func newEditor() editorState {
	return editorState{
		lines:      []string{""},
		mode:       editorModeNormal,
		vimEnabled: true,
	}
}

func (e *editorState) setContent(content string) {
	content = strings.ReplaceAll(content, "\r\n", "\n")
	e.lines = strings.Split(content, "\n")
	if len(e.lines) == 0 {
		e.lines = []string{""}
	}
	e.cursorRow = 0
	e.cursorCol = 0
	e.scrollRow = 0
	e.dirty = false
	if e.vimEnabled {
		e.mode = editorModeNormal
	} else {
		e.mode = editorModeInsert
	}
}

func (e *editorState) content() string {
	return strings.Join(e.lines, "\n")
}

func (e *editorState) line() string {
	if e.cursorRow < 0 || e.cursorRow >= len(e.lines) {
		return ""
	}
	return e.lines[e.cursorRow]
}

func (e *editorState) clampCursor() {
	if e.cursorRow < 0 {
		e.cursorRow = 0
	}
	if e.cursorRow >= len(e.lines) {
		e.cursorRow = len(e.lines) - 1
	}
	if e.cursorCol < 0 {
		e.cursorCol = 0
	}
	lineLen := len([]rune(e.line()))
	if e.cursorCol > lineLen {
		e.cursorCol = lineLen
	}
}

func (e *editorState) setVimEnabled(enabled bool) {
	e.vimEnabled = enabled
	if enabled {
		e.mode = editorModeNormal
	} else {
		e.mode = editorModeInsert
	}
}

func (e *editorState) moveLeft() {
	if e.cursorCol > 0 {
		e.cursorCol--
		return
	}
	if e.cursorRow > 0 {
		e.cursorRow--
		e.cursorCol = len([]rune(e.lines[e.cursorRow]))
	}
}

func (e *editorState) moveRight() {
	lineLen := len([]rune(e.line()))
	if e.cursorCol < lineLen {
		e.cursorCol++
		return
	}
	if e.cursorRow < len(e.lines)-1 {
		e.cursorRow++
		e.cursorCol = 0
	}
}

func (e *editorState) moveUp() {
	if e.cursorRow > 0 {
		e.cursorRow--
	}
	e.clampCursor()
}

func (e *editorState) moveDown() {
	if e.cursorRow < len(e.lines)-1 {
		e.cursorRow++
	}
	e.clampCursor()
}

func (e *editorState) insertRune(r rune) {
	lineRunes := []rune(e.line())
	lineRunes = append(lineRunes[:e.cursorCol], append([]rune{r}, lineRunes[e.cursorCol:]...)...)
	e.lines[e.cursorRow] = string(lineRunes)
	e.cursorCol++
	e.dirty = true
}

func (e *editorState) insertNewLine() {
	lineRunes := []rune(e.line())
	before := string(lineRunes[:e.cursorCol])
	after := string(lineRunes[e.cursorCol:])
	e.lines[e.cursorRow] = before
	next := append([]string{}, e.lines[:e.cursorRow+1]...)
	next = append(next, append([]string{after}, e.lines[e.cursorRow+1:]...)...)
	e.lines = next
	e.cursorRow++
	e.cursorCol = 0
	e.dirty = true
}

func (e *editorState) backspace() {
	if e.cursorCol > 0 {
		lineRunes := []rune(e.line())
		lineRunes = append(lineRunes[:e.cursorCol-1], lineRunes[e.cursorCol:]...)
		e.lines[e.cursorRow] = string(lineRunes)
		e.cursorCol--
		e.dirty = true
		return
	}

	if e.cursorRow == 0 {
		return
	}

	prevLen := len([]rune(e.lines[e.cursorRow-1]))
	e.lines[e.cursorRow-1] += e.lines[e.cursorRow]
	e.lines = append(e.lines[:e.cursorRow], e.lines[e.cursorRow+1:]...)
	e.cursorRow--
	e.cursorCol = prevLen
	e.dirty = true
}

func (e *editorState) deleteRune() {
	lineRunes := []rune(e.line())
	if e.cursorCol < len(lineRunes) {
		lineRunes = append(lineRunes[:e.cursorCol], lineRunes[e.cursorCol+1:]...)
		e.lines[e.cursorRow] = string(lineRunes)
		e.dirty = true
		return
	}

	if e.cursorRow >= len(e.lines)-1 {
		return
	}

	e.lines[e.cursorRow] += e.lines[e.cursorRow+1]
	e.lines = append(e.lines[:e.cursorRow+1], e.lines[e.cursorRow+2:]...)
	e.dirty = true
}
