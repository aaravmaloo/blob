package main

import (
	"fmt"
	"path/filepath"
	"strings"
	"time"

	"github.com/charmbracelet/bubbles/textinput"
	tea "github.com/charmbracelet/bubbletea"
	"github.com/charmbracelet/glamour"
	"github.com/charmbracelet/lipgloss"
	"github.com/sahilm/fuzzy"
)

type panel string

const (
	panelBrowser panel = "browser"
	panelEditor  panel = "editor"
	panelPreview panel = "preview"
)

type mode string

const (
	modeSplit   mode = "split"
	modeZen     mode = "zen"
	modePreview mode = "preview"
)

type visibleNode struct {
	node  *browserNode
	depth int
}

type model struct {
	storage *storage
	locks   *lockStore

	width  int
	height int

	rootNodes []*browserNode
	visible   []visibleNode
	selected  int
	collapsed map[string]bool

	activePanel    panel
	layoutMode     mode
	zenArmed       bool
	statusMessage  string
	selectedPath   string
	editor         editorState
	browserScroll  int
	showHelp       bool
	renaming       bool
	renameInput    string
	renameNodePath string
	renameIsFolder bool
	showChromeHint bool

	searchMode    bool
	searchInput   textinput.Model
	searchResults []fuzzy.Match
	allNotes      []noteEntry
	previewContent string
	glamourRender  *glamour.TermRenderer
	splashPhase    int
	splashStart    time.Time
	frame          int

	appLocked      bool
	lockPrompt     bool
	lockTarget     string
	lockInput      string
	lockIsSet      bool
	lockConfirm    string

	styles uiStyles
}

type noteEntry struct {
	path string
	name string
}

type uiStyles struct {
	app          lipgloss.Style
	topbar       lipgloss.Style
	title        lipgloss.Style
	subtitle     lipgloss.Style
	panel        lipgloss.Style
	panelFocused lipgloss.Style
	panelTitle   lipgloss.Style
	panelMeta    lipgloss.Style
	item         lipgloss.Style
	itemSelected lipgloss.Style
	itemMuted    lipgloss.Style
	folder       lipgloss.Style
	editorText   lipgloss.Style
	editorCursor lipgloss.Style
	statusBar    lipgloss.Style
	badge        lipgloss.Style
	badgeActive  lipgloss.Style
	badgeGold    lipgloss.Style
	badgeGreen   lipgloss.Style
	help         lipgloss.Style
	empty        lipgloss.Style
	icon         lipgloss.Style
	iconFolder   lipgloss.Style
	iconNote     lipgloss.Style
	searchBox    lipgloss.Style
	splash       lipgloss.Style
	previewBox   lipgloss.Style
	lockPrompt   lipgloss.Style
	lockIcon     lipgloss.Style
}

const (
	appPadX  = 1
	appPadY  = 0
	panelGap = 2
)

func newModel(storage *storage) (model, error) {
	if _, err := storage.ensureWelcomeNote(); err != nil {
		return model{}, err
	}

	searchInput := textinput.New()
	searchInput.Placeholder = "Search notes..."
	searchInput.CharLimit = 50
	searchInput.Width = 30

	gr, _ := glamour.NewTermRenderer(
		glamour.WithAutoStyle(),
		glamour.WithWordWrap(80),
	)

	locks := newLockStore(storage.rootDir)

	m := model{
		storage:        storage,
		locks:          locks,
		activePanel:    panelBrowser,
		layoutMode:     modeSplit,
		zenArmed:       false,
		statusMessage:  "BLOB is ready",
		editor:         newEditor(),
		collapsed:      make(map[string]bool),
		showChromeHint: true,
		searchInput:    searchInput,
		glamourRender:  gr,
		splashPhase:    0,
		splashStart:    time.Now(),
		frame:          0,
		appLocked:      locks.IsAppLocked(),
		lockPrompt:     locks.IsAppLocked(),
		lockTarget:     "app",
		lockInput:      "",
		lockIsSet:      false,
		lockConfirm:    "",
	}
	m.styles = buildStyles()
	m.collectAllNotes()

	if err := m.reloadTree(); err != nil {
		return model{}, err
	}

	return m, nil
}

func buildStyles() uiStyles {
	vars := loadThemeVars()
	surface := lipgloss.Color(vars["surface"])
	panelSurface := lipgloss.Color(vars["panel-surface"])
	panelEdge := lipgloss.Color(vars["panel-edge"])
	panelGlow := lipgloss.Color(vars["panel-glow"])
	ink := lipgloss.Color(vars["ink"])
	muted := lipgloss.Color(vars["muted"])
	accent := lipgloss.Color(vars["accent"])
	cyan := lipgloss.Color(vars["cyan"])
	rose := lipgloss.Color(vars["rose"])
	selectedBG := lipgloss.Color(vars["selected-bg"])
	selectedFG := lipgloss.Color(vars["selected-fg"])
	gold := lipgloss.Color(vars["gold"])
	green := lipgloss.Color(vars["green"])

	return uiStyles{
		app: lipgloss.NewStyle().
			Background(surface).
			Foreground(ink).
			Padding(appPadY, appPadX),
		topbar: lipgloss.NewStyle().
			Background(panelSurface).
			Foreground(ink).
			Border(lipgloss.RoundedBorder()).
			BorderForeground(panelEdge).
			Padding(0, 1).
			Margin(0, 0, 1, 0),
		title: lipgloss.NewStyle().
			Foreground(accent).
			Bold(true).
			Underline(true),
		subtitle: lipgloss.NewStyle().
			Foreground(muted),
		panel: lipgloss.NewStyle().
			Background(panelSurface).
			Border(lipgloss.NormalBorder()).
			BorderForeground(panelEdge).
			BorderTop(true).
			BorderBottom(true).
			BorderLeft(true).
			BorderRight(true).
			Padding(1),
		panelFocused: lipgloss.NewStyle().
			Background(panelSurface).
			Border(lipgloss.NormalBorder()).
			BorderForeground(panelGlow).
			BorderTop(true).
			BorderBottom(true).
			BorderLeft(true).
			BorderRight(true).
			Padding(1),
		panelTitle: lipgloss.NewStyle().
			Foreground(accent).
			Bold(true).
			MarginBottom(1),
		panelMeta: lipgloss.NewStyle().
			Foreground(muted).
			Faint(true),
		item: lipgloss.NewStyle().
			Foreground(ink).
			PaddingLeft(1),
		itemSelected: lipgloss.NewStyle().
			Foreground(selectedFG).
			Background(selectedBG).
			Bold(true).
			Padding(0, 1).
			BorderLeft(true).
			BorderForeground(panelGlow),
		itemMuted: lipgloss.NewStyle().
			Foreground(muted).
			Faint(true),
		folder: lipgloss.NewStyle().
			Foreground(rose).
			Bold(true),
		editorText: lipgloss.NewStyle().
			Foreground(ink).
			PaddingLeft(1),
		editorCursor: lipgloss.NewStyle().
			Background(cyan).
			Foreground(surface).
			Bold(true),
		statusBar: lipgloss.NewStyle().
			Background(panelSurface).
			Foreground(ink).
			Border(lipgloss.RoundedBorder()).
			BorderForeground(panelEdge).
			Padding(0, 1).
			Margin(1, 0, 0, 0),
		badge: lipgloss.NewStyle().
			Foreground(muted).
			Border(lipgloss.RoundedBorder()).
			BorderForeground(panelEdge).
			Padding(0, 1),
		badgeActive: lipgloss.NewStyle().
			Foreground(selectedFG).
			Background(selectedBG).
			Bold(true).
			Padding(0, 1),
		badgeGold: lipgloss.NewStyle().
			Foreground(surface).
			Background(gold).
			Bold(true).
			Padding(0, 1),
		badgeGreen: lipgloss.NewStyle().
			Foreground(surface).
			Background(green).
			Bold(true).
			Padding(0, 1),
		help: lipgloss.NewStyle().
			Foreground(muted).
			Faint(true),
		empty: lipgloss.NewStyle().
			Foreground(muted).
			Italic(true).
			Faint(true),
		icon: lipgloss.NewStyle().
			Foreground(accent),
		iconFolder: lipgloss.NewStyle().
			Foreground(rose),
		iconNote: lipgloss.NewStyle().
			Foreground(cyan),
		searchBox: lipgloss.NewStyle().
			Background(panelSurface).
			Foreground(ink).
			Border(lipgloss.RoundedBorder()).
			BorderForeground(panelGlow).
			Padding(0, 1).
			Margin(1, 0),
		splash: lipgloss.NewStyle().
			Foreground(accent).
			Bold(true).
			Align(lipgloss.Center),
		previewBox: lipgloss.NewStyle().
			Background(panelSurface).
			Border(lipgloss.RoundedBorder()).
			BorderForeground(panelGlow).
			Padding(1),
		lockPrompt: lipgloss.NewStyle().
			Background(panelSurface).
			Foreground(ink).
			Border(lipgloss.RoundedBorder()).
			BorderForeground(panelGlow).
			Padding(1, 2).
			Align(lipgloss.Center),
		lockIcon: lipgloss.NewStyle().
			Foreground(gold).
			Bold(true),
	}
}

func (m model) Init() tea.Cmd {
	return frameTick()
}

func frameTick() tea.Cmd {
	return tea.Tick(time.Second/30, func(t time.Time) tea.Msg {
		return tickMsg{time: t}
	})
}

type tickMsg struct {
	time time.Time
}

func (m model) Update(msg tea.Msg) (tea.Model, tea.Cmd) {
	switch msg := msg.(type) {
	case tea.WindowSizeMsg:
		m.width = msg.Width
		m.height = msg.Height
		m.searchInput.Width = max(20, m.width/3)
		return m, nil
	case tickMsg:
		m.frame++
		if m.splashPhase < 3 && time.Since(m.splashStart) > time.Duration(m.splashPhase+1)*400*time.Millisecond {
			m.splashPhase++
		}
		return m, frameTick()
	case tea.KeyMsg:
		if msg.String() == "ctrl+c" || (msg.String() == "q" && m.canQuitWithQ()) {
			return m, tea.Quit
		}
		return m.handleKey(msg), nil
	}
	return m, nil
}

func (m model) handleKey(msg tea.KeyMsg) model {
	key := msg.String()

	if m.lockPrompt {
		return m.handleLockKey(msg)
	}

	if m.appLocked {
		return m
	}

	inInsert := m.activePanel == panelEditor && (!m.editor.vimEnabled || m.editor.mode == editorModeInsert)

	if m.searchMode {
		return m.handleSearchKey(msg)
	}

	if m.renaming {
		return m.handleRenameKey(msg)
	}
	// While actively editing, disable global shortcuts so typing never triggers app actions.
	if inInsert {
		return m.handleEditorKey(msg)
	}
	if key == "H" || key == "ctrl+h" || (key == "h" && m.activePanel == panelBrowser) {
		m.showHelp = !m.showHelp
		if m.showHelp {
			m.statusMessage = "Help open"
		} else {
			m.statusMessage = "Help closed"
		}
		return m
	}
	if m.showHelp && (key == "esc" || key == "enter") {
		m.showHelp = false
		m.statusMessage = "Help closed"
		return m
	}

	switch key {
	case "n":
		if m.activePanel != panelBrowser {
			break
		}
		if err := m.createNote(); err != nil {
			m.statusMessage = err.Error()
		}
		return m
	case "N", "shift+n":
		if m.activePanel != panelBrowser {
			break
		}
		if err := m.createFolder(); err != nil {
			m.statusMessage = err.Error()
		}
		return m
	case "p", "tab":
		if inInsert {
			break
		}
		m.togglePanel()
		return m
	case "z":
		if inInsert {
			break
		}
		m.toggleZenFromSelection()
		return m
	case "d", "ctrl+d":
		if inInsert || m.activePanel != panelBrowser {
			break
		}
		if err := m.deleteSelectedNote(); err != nil {
			m.statusMessage = err.Error()
		}
		return m
	case "r", "ctrl+r", "f2":
		if m.activePanel != panelBrowser {
			break
		}
		node := m.currentNode()
		if node == nil {
			return m
		}
		m.renaming = true
		m.renameInput = node.name
		m.renameNodePath = node.path
		m.renameIsFolder = node.isFolder
		m.statusMessage = "Rename: type new name, Enter to save, Esc to cancel"
		return m
	case "/":
		if inInsert {
			break
		}
		m.searchMode = true
		m.searchInput.Focus()
		m.searchInput.SetValue("")
		m.statusMessage = "Search mode - type to filter notes"
		return m
	case "m":
		if inInsert || m.activePanel != panelEditor {
			break
		}
		if m.layoutMode == modePreview {
			m.layoutMode = modeSplit
			m.statusMessage = "Split mode"
		} else {
			m.updatePreview()
			m.layoutMode = modePreview
			m.statusMessage = "Markdown preview mode"
		}
		return m
	case "v":
		if inInsert {
			break
		}
		m.editor.setVimEnabled(!m.editor.vimEnabled)
		if m.editor.vimEnabled {
			m.statusMessage = "Vim editing enabled"
		} else {
			m.statusMessage = "Vim editing disabled"
		}
		return m
	case "L":
		if inInsert {
			break
		}
		node := m.currentNode()
		if node == nil {
			return m
		}
		if m.locks.IsItemLocked(node.path) {
			m.locks.UnlockItem(node.path)
			m.statusMessage = "Unlocked: " + node.name
			return m
		}
		m.lockPrompt = true
		m.lockTarget = node.path
		m.lockInput = ""
		m.lockIsSet = true
		m.lockConfirm = ""
		m.statusMessage = "Set password for " + node.name
		return m
	case "ctrl+l":
		if inInsert {
			break
		}
		if m.locks.IsAppLocked() {
			m.appLocked = true
			m.lockPrompt = true
			m.lockTarget = "app"
			m.lockInput = ""
			m.lockIsSet = false
			m.lockConfirm = ""
			m.statusMessage = "App locked - enter password"
		} else {
			m.lockPrompt = true
			m.lockTarget = "app:set"
			m.lockInput = ""
			m.lockIsSet = true
			m.lockConfirm = ""
			m.statusMessage = "Set app password"
		}
		return m
	case "ctrl+n":
		if err := m.createNote(); err != nil {
			m.statusMessage = err.Error()
		}
		return m
	case "ctrl+shift+n":
		if err := m.createFolder(); err != nil {
			m.statusMessage = err.Error()
		}
		return m
	case "ctrl+shift+p", "P":
		m.togglePanel()
		return m
	case "ctrl+shift+f", "F":
		m.zenArmed = !m.zenArmed
		if !m.zenArmed && m.layoutMode == modeZen {
			m.layoutMode = modeSplit
		}
		if m.zenArmed {
			m.statusMessage = "Zen armed"
		} else {
			m.statusMessage = "Zen toggle disabled"
		}
		return m
	case "ctrl+shift+v", "V":
		m.editor.setVimEnabled(!m.editor.vimEnabled)
		if m.editor.vimEnabled {
			m.statusMessage = "Vim editing enabled"
		} else {
			m.statusMessage = "Vim editing disabled"
		}
		return m
	}

	if m.activePanel == panelBrowser {
		return m.handleBrowserKey(msg)
	}
	return m.handleEditorKey(msg)
}

func (m model) handleLockKey(msg tea.KeyMsg) model {
	switch msg.Type {
	case tea.KeyEsc:
		m.lockPrompt = false
		m.lockInput = ""
		m.lockConfirm = ""
		m.lockTarget = ""
		m.statusMessage = "Cancelled"
		return m
	case tea.KeyEnter:
		if m.lockIsSet && m.lockConfirm == "" {
			m.lockConfirm = m.lockInput
			m.lockInput = ""
			m.statusMessage = "Confirm password"
			return m
		}
		if m.lockIsSet && m.lockConfirm != "" {
			if m.lockInput != m.lockConfirm {
				m.lockPrompt = false
				m.lockInput = ""
				m.lockConfirm = ""
				m.lockTarget = ""
				m.statusMessage = "Passwords did not match"
				return m
			}
			if m.lockTarget == "app:set" {
				if err := m.locks.SetAppLock(m.lockInput); err != nil {
					m.statusMessage = err.Error()
				} else {
					m.statusMessage = "App password set"
				}
			} else {
				if err := m.locks.LockItem(m.lockTarget, m.lockInput); err != nil {
					m.statusMessage = err.Error()
				} else {
					m.statusMessage = "Locked"
				}
			}
			m.lockPrompt = false
			m.lockInput = ""
			m.lockConfirm = ""
			m.lockTarget = ""
			return m
		}
		if m.lockTarget == "app" {
			if m.locks.CheckAppPassword(m.lockInput) {
				m.appLocked = false
				m.lockPrompt = false
				m.lockInput = ""
				m.lockTarget = ""
				m.statusMessage = "App unlocked"
			} else {
				m.lockInput = ""
				m.statusMessage = "Wrong password"
			}
			return m
		}
		if m.locks.CheckItemPassword(m.lockTarget, m.lockInput) {
			m.lockPrompt = false
			m.lockInput = ""
			unlockedPath := m.lockTarget
			m.lockTarget = ""
			m.statusMessage = "Unlocked"
			node := m.currentNode()
			if node != nil && node.path == unlockedPath {
				if node.isFolder {
					node.collapsed = !node.collapsed
					m.collapsed[node.path] = node.collapsed
					_ = m.reloadTree()
				} else {
					m.activePanel = panelEditor
					m.statusMessage = "Editor focused"
				}
			}
		} else {
			m.lockInput = ""
			m.statusMessage = "Wrong password"
		}
		return m
	case tea.KeyBackspace:
		if len(m.lockInput) > 0 {
			r := []rune(m.lockInput)
			m.lockInput = string(r[:len(r)-1])
		}
		return m
	case tea.KeyRunes:
		m.lockInput += string(msg.Runes)
		return m
	}
	return m
}

func (m model) handleRenameKey(msg tea.KeyMsg) model {
	switch msg.Type {
	case tea.KeyEsc:
		m.renaming = false
		m.renameInput = ""
		m.renameNodePath = ""
		m.statusMessage = "Rename canceled"
		return m
	case tea.KeyEnter:
		newPath, err := m.storage.renameNode(m.renameNodePath, m.renameInput, m.renameIsFolder)
		if err != nil {
			m.statusMessage = err.Error()
			return m
		}
		m.renaming = false
		m.renameInput = ""
		m.renameNodePath = ""
		m.selectedPath = newPath
		_ = m.reloadTree()
		m.statusMessage = "Renamed"
		return m
	case tea.KeyBackspace:
		if len(m.renameInput) > 0 {
			r := []rune(m.renameInput)
			m.renameInput = string(r[:len(r)-1])
		}
		return m
	case tea.KeySpace:
		m.renameInput += " "
		return m
	case tea.KeyRunes:
		m.renameInput += string(msg.Runes)
		return m
	}
	return m
}

func (m model) handleSearchKey(msg tea.KeyMsg) model {
	switch msg.Type {
	case tea.KeyEsc:
		m.searchMode = false
		m.searchInput.Blur()
		m.searchInput.SetValue("")
		m.searchResults = nil
		m.statusMessage = "Search closed"
		return m
	case tea.KeyEnter:
		if len(m.searchResults) > 0 {
			path := m.allNotes[m.searchResults[0].Index].path
			m.selectedPath = path
			m.activePanel = panelEditor
			_ = m.reloadTree()
			m.searchMode = false
			m.searchInput.Blur()
			m.searchResults = nil
			m.statusMessage = "Opened search result"
		}
		return m
	}

	var cmd tea.Cmd
	m.searchInput, cmd = m.searchInput.Update(msg)

	query := m.searchInput.Value()
	if query != "" {
		names := make([]string, len(m.allNotes))
		for i, n := range m.allNotes {
			names[i] = n.name
		}
		m.searchResults = fuzzy.Find(query, names)
	} else {
		m.searchResults = nil
	}

	_ = cmd
	return m
}

func (m *model) updatePreview() {
	if m.glamourRender == nil {
		return
	}
	content := m.editor.content()
	if content == "" {
		m.previewContent = ""
		return
	}
	rendered, err := m.glamourRender.Render(content)
	if err != nil {
		m.previewContent = content
		return
	}
	m.previewContent = rendered
}

func (m *model) collectAllNotes() {
	nodes, _ := m.storage.loadTree()
	m.allNotes = nil
	m.collectNotesFromNodes(nodes)
}

func (m *model) collectNotesFromNodes(nodes []*browserNode) {
	for _, n := range nodes {
		if n.isFolder {
			m.collectNotesFromNodes(n.children)
		} else {
			m.allNotes = append(m.allNotes, noteEntry{path: n.path, name: n.name})
		}
	}
}

func (m model) handleBrowserKey(msg tea.KeyMsg) model {
	switch msg.String() {
	case "up", "k":
		if m.selected > 0 {
			m.selected--
		}
	case "down", "j":
		if m.selected < len(m.visible)-1 {
			m.selected++
		}
	case "enter":
		node := m.currentNode()
		if node == nil {
			return m
		}
		if m.locks.IsItemLocked(node.path) {
			m.lockPrompt = true
			m.lockTarget = node.path
			m.lockInput = ""
			m.lockIsSet = false
			m.lockConfirm = ""
			m.statusMessage = "Locked - enter password"
			m.syncSelection()
			return m
		}
		if node.isFolder {
			node.collapsed = !node.collapsed
			m.collapsed[node.path] = node.collapsed
			if node.collapsed {
				m.statusMessage = "Folder folded"
			} else {
				m.statusMessage = "Folder expanded"
			}
			_ = m.reloadTree()
			return m
		}
		m.activePanel = panelEditor
		m.statusMessage = "Editor focused"
	case "shift+enter", "ctrl+enter":
		node := m.currentNode()
		if node == nil || node.isFolder {
			return m
		}
		m.toggleZenFromSelection()
	}
	m.syncSelection()
	return m
}

func (m *model) toggleZenFromSelection() {
	if m.layoutMode == modeZen {
		m.layoutMode = modeSplit
		m.statusMessage = "Split mode"
		return
	}
	node := m.currentNode()
	if node == nil || node.isFolder {
		m.statusMessage = "Select a note for zen mode"
		return
	}
	m.layoutMode = modeZen
	m.activePanel = panelEditor
	m.statusMessage = "Zen mode"
}

func (m *model) deleteSelectedNote() error {
	node := m.currentNode()
	if node == nil {
		return nil
	}
	if node.isFolder {
		if err := m.storage.deleteFolder(node.path); err != nil {
			return err
		}
		m.selectedPath = ""
		if err := m.reloadTree(); err != nil {
			return err
		}
		m.statusMessage = "Deleted folder"
		return nil
	}
	if err := m.storage.deleteNote(node.path); err != nil {
		return err
	}
	m.selectedPath = ""
	if err := m.reloadTree(); err != nil {
		return err
	}
	m.statusMessage = "Deleted note"
	return nil
}

func (m model) handleEditorKey(msg tea.KeyMsg) model {
	key := msg.String()

	if key == "esc" && m.editor.vimEnabled {
		m.editor.mode = editorModeNormal
		m.statusMessage = "Normal mode"
		return m
	}

	if !m.editor.vimEnabled || m.editor.mode == editorModeInsert {
		return m.handleInsertKey(msg)
	}
	return m.handleNormalKey(msg)
}

func (m model) handleNormalKey(msg tea.KeyMsg) model {
	switch msg.String() {
	case "h", "left":
		m.editor.moveLeft()
	case "l", "right":
		m.editor.moveRight()
	case "k", "up":
		m.editor.moveUp()
	case "j", "down":
		m.editor.moveDown()
	case "0":
		m.editor.cursorCol = 0
	case "$":
		m.editor.cursorCol = len([]rune(m.editor.line()))
	case "i":
		m.editor.mode = editorModeInsert
		m.statusMessage = "Insert mode"
	case "a":
		m.editor.moveRight()
		m.editor.mode = editorModeInsert
		m.statusMessage = "Insert mode"
	case "o":
		m.editor.cursorCol = len([]rune(m.editor.line()))
		m.editor.insertNewLine()
		m.editor.mode = editorModeInsert
		m.saveCurrentNote()
		m.statusMessage = "Insert mode"
	case "x":
		m.editor.deleteRune()
		m.saveCurrentNote()
		m.statusMessage = "Saved"
	}

	m.ensureEditorScroll()
	return m
}

func (m model) handleInsertKey(msg tea.KeyMsg) model {
	switch msg.Type {
	case tea.KeyRunes:
		for _, r := range msg.Runes {
			m.editor.insertRune(r)
		}
	case tea.KeySpace:
		m.editor.insertRune(' ')
	case tea.KeyEnter:
		m.editor.insertNewLine()
	case tea.KeyBackspace:
		m.editor.backspace()
	case tea.KeyDelete:
		m.editor.deleteRune()
	case tea.KeyLeft:
		m.editor.moveLeft()
	case tea.KeyRight:
		m.editor.moveRight()
	case tea.KeyUp:
		m.editor.moveUp()
	case tea.KeyDown:
		m.editor.moveDown()
	case tea.KeyTab:
		m.editor.insertRune('\t')
	}

	if m.editor.dirty {
		m.saveCurrentNote()
		m.statusMessage = "Saved"
	}
	m.ensureEditorScroll()
	return m
}

func (m *model) createFolder() error {
	path, err := m.storage.createFolder(m.currentNode())
	if err != nil {
		return err
	}
	m.statusMessage = "Created folder " + filepath.Base(path)
	return m.reloadTreeAndSelect(path)
}

func (m *model) createNote() error {
	path, err := m.storage.createNote(m.currentNode())
	if err != nil {
		return err
	}
	m.statusMessage = "Created note " + strings.TrimSuffix(filepath.Base(path), noteExtension)
	return m.reloadTreeAndSelect(path)
}

func (m *model) saveCurrentNote() {
	if m.selectedPath == "" {
		return
	}
	if err := m.storage.writeNote(m.selectedPath, m.editor.content()); err != nil {
		m.statusMessage = err.Error()
		return
	}
	m.editor.dirty = false
	if m.layoutMode == modePreview {
		m.updatePreview()
	}
}

func (m *model) togglePanel() {
	if m.activePanel == panelBrowser {
		m.activePanel = panelEditor
		m.statusMessage = "Editor focused"
		return
	}
	m.activePanel = panelBrowser
	m.statusMessage = "Browser focused"
}

func (m *model) reloadTreeAndSelect(path string) error {
	m.selectedPath = path
	return m.reloadTree()
}

func (m *model) reloadTree() error {
	nodes, err := m.storage.loadTree()
	if err != nil {
		return err
	}
	applyCollapsedState(nodes, m.collapsed)
	m.rootNodes = nodes
	m.visible = flattenNodes(nodes, 0)
	if len(m.visible) == 0 {
		m.selected = 0
		m.selectedPath = ""
		m.editor.setContent("")
		return nil
	}

	if m.selectedPath != "" {
		for i, item := range m.visible {
			if item.node.path == m.selectedPath {
				m.selected = i
				break
			}
		}
	}
	if m.selected >= len(m.visible) {
		m.selected = len(m.visible) - 1
	}
	return m.syncSelection()
}

func flattenNodes(nodes []*browserNode, depth int) []visibleNode {
	items := make([]visibleNode, 0)
	for _, node := range nodes {
		items = append(items, visibleNode{node: node, depth: depth})
		if node.isFolder && !node.collapsed {
			items = append(items, flattenNodes(node.children, depth+1)...)
		}
	}
	return items
}

func applyCollapsedState(nodes []*browserNode, collapsed map[string]bool) {
	for _, node := range nodes {
		if node.isFolder {
			node.collapsed = collapsed[node.path]
			applyCollapsedState(node.children, collapsed)
		}
	}
}

func (m *model) currentNode() *browserNode {
	if len(m.visible) == 0 || m.selected < 0 || m.selected >= len(m.visible) {
		return nil
	}
	return m.visible[m.selected].node
}

func (m *model) syncSelection() error {
	node := m.currentNode()
	if node == nil {
		return nil
	}

	m.selectedPath = node.path
	if node.isFolder {
		return nil
	}

	content, err := m.storage.readNote(node.path)
	if err != nil {
		return err
	}
	m.editor.setContent(content)
	m.ensureEditorScroll()
	return nil
}

func (m *model) ensureEditorScroll() {
	editorHeight := max(3, m.height-10)
	if m.editor.cursorRow < m.editor.scrollRow {
		m.editor.scrollRow = m.editor.cursorRow
	}
	if m.editor.cursorRow >= m.editor.scrollRow+editorHeight {
		m.editor.scrollRow = m.editor.cursorRow - editorHeight + 1
	}
}

func (m model) canQuitWithQ() bool {
	if m.activePanel != panelEditor {
		return true
	}
	if !m.editor.vimEnabled {
		return false
	}
	return m.editor.mode == editorModeNormal
}

func (m model) View() string {
	if m.width == 0 || m.height == 0 {
		return ""
	}

	if m.splashPhase < 3 {
		return m.renderSplash()
	}

	contentWidth := max(20, m.width-(appPadX*2))
	status := m.renderStatus(contentWidth)
	availableHeight := m.height - lipgloss.Height(status)
	if availableHeight < 10 {
		availableHeight = 10
	}

	body := ""
	if m.layoutMode == modeZen {
		body = m.renderEditorPanel(contentWidth, availableHeight)
	} else if m.layoutMode == modePreview {
		gap := panelGap
		browserWidth := max(28, contentWidth/4)
		editorWidth := (contentWidth - browserWidth - gap) / 2
		previewWidth := contentWidth - browserWidth - editorWidth - gap*2
		if editorWidth < 30 {
			editorWidth = 30
			browserWidth = max(20, (contentWidth-editorWidth*2-gap*2)/2)
			previewWidth = editorWidth
		}
		browser := m.renderBrowserPanel(browserWidth, availableHeight)
		editor := m.renderEditorPanel(editorWidth, availableHeight)
		preview := m.renderPreviewPanel(previewWidth, availableHeight)
		body = lipgloss.JoinHorizontal(lipgloss.Top, browser, "  ", editor, "  ", preview)
	} else {
		gap := panelGap
		browserWidth := max(28, contentWidth/3)
		editorWidth := contentWidth - browserWidth - gap
		if editorWidth < 40 {
			editorWidth = 40
			browserWidth = max(20, contentWidth-editorWidth-gap)
		}
		if browserWidth+editorWidth+gap > contentWidth {
			editorWidth = max(20, contentWidth-browserWidth-gap)
		}
		if editorWidth < 20 {
			browserWidth = max(20, (contentWidth-gap)/2)
			editorWidth = max(20, contentWidth-browserWidth-gap)
		}
		browser := m.renderBrowserPanel(browserWidth, availableHeight)
		editor := m.renderEditorPanel(editorWidth, availableHeight)
		body = lipgloss.JoinHorizontal(lipgloss.Top, browser, "  ", editor)
	}

	root := lipgloss.JoinVertical(lipgloss.Left, body, status)
	if m.appLocked && !m.lockPrompt {
		return m.styles.app.Render(m.renderAppLockScreen(contentWidth))
	}
	if m.lockPrompt {
		return m.styles.app.Render(m.renderLockPrompt(contentWidth))
	}
	if m.showHelp {
		return m.styles.app.Render(m.renderHelpPanel(contentWidth))
	}
	if m.searchMode {
		root = m.overlaySearch(root, contentWidth)
	}
	return m.styles.app.Render(root)
}

func (m model) renderSplash() string {
	frames := []string{
		"BLOB",
		"BLOB .",
		"BLOB . .",
		"BLOB . . .",
	}
	frame := m.splashPhase
	if frame >= len(frames) {
		frame = len(frames) - 1
	}
	text := frames[frame]
	return lipgloss.Place(m.width, m.height, lipgloss.Center, lipgloss.Center, m.styles.splash.Render(text))
}

func (m model) renderAppLockScreen(width int) string {
	lockIcon := m.styles.lockIcon.Render("[LOCKED]")
	title := m.styles.title.Render("BLOB is locked")
	hint := m.styles.help.Render("Press Ctrl+L to unlock")
	content := lipgloss.JoinVertical(lipgloss.Center, lockIcon, "", title, "", hint)
	box := m.styles.lockPrompt.Width(min(width-10, 40)).Render(content)
	return lipgloss.Place(width, m.height, lipgloss.Center, lipgloss.Center, box)
}

func (m model) renderLockPrompt(width int) string {
	maskedInput := strings.Repeat("*", len(m.lockInput))
	label := "Enter password"
	if m.lockIsSet && m.lockConfirm != "" {
		label = "Confirm password"
	} else if m.lockIsSet {
		label = "Set password"
	}
	lockIcon := m.styles.lockIcon.Render("[LOCK]")
	promptLabel := m.styles.panelTitle.Render(label)
	input := m.styles.editorText.Render(maskedInput)
	cursor := m.styles.editorCursor.Render(" ")
	hint := m.styles.help.Render("Enter to confirm, Esc to cancel")
	statusLine := m.styles.help.Render(m.statusMessage)
	content := lipgloss.JoinVertical(lipgloss.Center, lockIcon, "", promptLabel, "", input+cursor, "", hint, statusLine)
	box := m.styles.lockPrompt.Width(min(width-10, 40)).Render(content)
	return lipgloss.Place(width, m.height, lipgloss.Center, lipgloss.Center, box)
}

func (m model) renderTopbar(width int) string {
	left := m.styles.title.Render("BLOB")
	modeTag := m.styles.badge.Render(modeLabel(m.layoutMode))
	focusTag := m.styles.badge.Render(labelForPanel(m.activePanel))
	vimTag := m.styles.badge.Render("vim " + onOff(m.editor.vimEnabled))
	if m.editor.vimEnabled {
		vimTag = m.styles.badgeActive.Render("vim on")
	}
	right := lipgloss.JoinHorizontal(lipgloss.Left, modeTag, "  ", focusTag, "  ", vimTag)
	content := lipgloss.JoinHorizontal(lipgloss.Left, left, "  ", right)
	return m.styles.topbar.Width(width).Render(content)
}

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func (m model) overlaySearch(content string, width int) string {
	searchBox := m.styles.searchBox.Width(min(width-10, 50)).Render(m.searchInput.View())
	results := ""
	if len(m.searchResults) > 0 {
		lines := []string{}
		for i, r := range m.searchResults {
			if i >= 5 {
				break
			}
			name := m.allNotes[r.Index].name
			if i == 0 {
				lines = append(lines, m.styles.itemSelected.Render(name))
			} else {
				lines = append(lines, m.styles.item.Render(name))
			}
		}
		results = strings.Join(lines, "\n")
	} else if m.searchInput.Value() != "" {
		results = m.styles.empty.Render("No results")
	}
	searchPanel := lipgloss.JoinVertical(lipgloss.Left, searchBox, results)
	return lipgloss.Place(width, m.height, lipgloss.Center, lipgloss.Center, searchPanel)
}

func (m model) renderPreviewPanel(width, height int) string {
	style := m.styles.previewBox.Width(width).Height(height)
	if m.activePanel == panelPreview {
		style = m.styles.panelFocused.Width(width).Height(height)
	}
	node := m.currentNode()
	title := "Preview"
	if node != nil && !node.isFolder {
		title = node.name + " (preview)"
	}
	header := m.styles.panelTitle.Render(truncateToWidth(title, width-4))
	lines := []string{header, ""}
	if m.previewContent == "" {
		lines = append(lines, m.styles.empty.Render("Press m to preview markdown"))
	} else {
		lines = append(lines, m.previewContent)
	}
	return style.Render(strings.Join(lines, "\n"))
}

func joinBar(left, right string, width int) string {
	leftW := lipgloss.Width(left)
	rightW := lipgloss.Width(right)
	if leftW+rightW+1 <= width {
		return left + strings.Repeat(" ", width-leftW-rightW) + right
	}
	if leftW >= width {
		return truncateToWidth(left, width)
	}
	maxRight := max(1, width-leftW-1)
	return left + " " + truncateToWidth(right, maxRight)
}

func (m model) renderBrowserPanel(width, height int) string {
	style := m.styles.panel.Width(width).Height(height)
	if m.activePanel == panelBrowser {
		style = m.styles.panelFocused.Width(width).Height(height)
	}

	lines := []string{
		lipgloss.JoinHorizontal(lipgloss.Left, m.styles.panelTitle.Render("Notes"), " ", m.styles.panelMeta.Render(fmt.Sprintf("%d", len(m.visible)))),
		"",
	}

	if len(m.visible) == 0 {
		lines = append(lines, m.styles.empty.Render("Press Ctrl+N to create a note"))
		return style.Render(strings.Join(lines, "\n"))
	}

	maxLines := max(3, height-4)
	if m.selected < m.browserScroll {
		m.browserScroll = m.selected
	}
	if m.selected >= m.browserScroll+maxLines {
		m.browserScroll = m.selected - maxLines + 1
	}

	for i := m.browserScroll; i < len(m.visible) && len(lines) < maxLines+2; i++ {
		item := m.visible[i]
		indent := strings.Repeat("  ", item.depth)
		label := item.node.name
		icon := ""
		if item.node.isFolder {
			if item.node.collapsed {
				icon = m.styles.iconFolder.Render("+")
			} else {
				icon = m.styles.iconFolder.Render("-")
			}
			label = icon + " " + label
		} else {
			icon = m.styles.iconNote.Render("•")
			label = icon + " " + label
		}
		if m.locks.IsItemLocked(item.node.path) {
			label += " " + m.styles.lockIcon.Render("[*]")
		}

		row := indent + label
		if i == m.selected {
			lines = append(lines, m.styles.itemSelected.Render(row))
			continue
		}

		textStyle := m.styles.item
		if item.node.isFolder {
			textStyle = m.styles.folder
		}
		lines = append(lines, textStyle.Render(row))
	}

	return style.Render(strings.Join(lines, "\n"))
}

func (m model) renderEditorPanel(width, height int) string {
	style := m.styles.panel.Width(width).Height(height)
	if m.activePanel == panelEditor {
		style = m.styles.panelFocused.Width(width).Height(height)
	}

	node := m.currentNode()
	title := "No note selected"
	meta := "Pick a note from the list"
	if node != nil {
		title = node.name
		if node.isFolder {
			meta = "Folder"
		} else {
			meta = filepath.Dir(strings.TrimPrefix(node.path, m.storage.rootDir))
			if meta == "." || meta == string(filepath.Separator) {
				meta = "home"
			}
		}
	}

	header := lipgloss.JoinHorizontal(
		lipgloss.Left,
		m.styles.panelTitle.Render(truncateToWidth(title, max(1, width/2))),
		" ",
		m.styles.panelMeta.Render(truncateToWidth(meta, max(1, width/2))),
	)
	lines := []string{header, ""}

	if node == nil || node.isFolder {
		lines = append(lines, m.styles.empty.Render("No note open"))
		return style.Render(strings.Join(lines, "\n"))
	}

	bodyHeight := max(3, height-6)
	lines = append(lines, m.renderEditorBody(bodyHeight, width-4)...)
	lines = append(lines, "")

	currentMode := "INSERT"
	if m.editor.mode == editorModeNormal {
		currentMode = "NORMAL"
	}
	lines = append(lines, m.styles.help.Render(currentMode))

	return style.Render(strings.Join(lines, "\n"))
}

func (m model) renderStatus(width int) string {
	innerWidth := max(10, width-4)
	mode := "insert"
	if m.editor.mode == editorModeNormal {
		mode = "normal"
	}
	layout := modeLabel(m.layoutMode)
	focus := labelForPanel(m.activePanel)
	vim := onOff(m.editor.vimEnabled)
	leftRaw := "status: " + m.statusMessage + " | layout: " + layout + " | focus: " + focus + " | vim: " + vim + " | mode: " + mode
	left := truncateToWidth(leftRaw, innerWidth)
	if m.renaming {
		right := m.styles.help.Render("Name: " + truncateToWidth(m.renameInput, max(10, innerWidth/2)))
		return m.styles.statusBar.Width(width).Render(joinBar(left, right, innerWidth))
	}
	right := m.styles.help.Render("n note  N folder  r rename  d delete  L lock  Ctrl+L app lock  / search  m preview  h help")
	content := joinBar(left, right, innerWidth)
	return m.styles.statusBar.Width(width).Render(content)
}

func (m model) renderHelpPanel(width int) string {
	inner := max(40, width-4)
	box := m.styles.panelFocused.Width(inner).Render(strings.Join([]string{
		m.styles.panelTitle.Render("BLOB Help"),
		"",
		m.styles.item.Render("n               new note (browser)"),
		m.styles.item.Render("N               new folder (browser)"),
		m.styles.item.Render("r               rename selected"),
		m.styles.item.Render("d               delete selected note/folder"),
		m.styles.item.Render("p / tab         switch panel"),
		m.styles.item.Render("z               toggle zen"),
		m.styles.item.Render("/               search notes"),
		m.styles.item.Render("m               markdown preview"),
		m.styles.item.Render("v               vim on/off"),
		m.styles.item.Render("L               lock/unlock item"),
		m.styles.item.Render("Ctrl+L          lock/unlock app"),
		m.styles.item.Render("h / H / Ctrl+H  help"),
		m.styles.item.Render("q               quit (normal mode)"),
		"",
		m.styles.help.Render("Esc closes help"),
	}, "\n"))
	return lipgloss.Place(width, max(14, m.height-(appPadY*2)), lipgloss.Center, lipgloss.Center, box)
}

func (m model) renderEditorBody(bodyHeight, width int) []string {
	out := make([]string, 0, bodyHeight)
	for i := 0; i < bodyHeight; i++ {
		lineIndex := m.editor.scrollRow + i
		if lineIndex >= len(m.editor.lines) {
			out = append(out, m.styles.empty.Render("~"))
			continue
		}

		line := m.editor.lines[lineIndex]
		rendered := []rune(line)
		if lineIndex == m.editor.cursorRow {
			renderedLine := m.renderCursorLine(rendered, width)
			out = append(out, renderedLine)
			continue
		}

		out = append(out, m.styles.editorText.Render(truncateRunes(rendered, width)))
	}
	return out
}

func (m model) renderCursorLine(line []rune, width int) string {
	cursorCol := m.editor.cursorCol
	if cursorCol > len(line) {
		cursorCol = len(line)
	}

	prefix := truncateRunes(line[:cursorCol], width)
	cursor := " "
	if cursorCol < len(line) {
		cursor = string(line[cursorCol])
	}
	suffix := ""
	if cursorCol < len(line) {
		suffix = truncateRunes(line[cursorCol+1:], max(0, width-len([]rune(prefix))-1))
	}

	return m.styles.editorText.Render(prefix) + m.styles.editorCursor.Render(cursor) + m.styles.editorText.Render(suffix)
}

func truncateRunes(runes []rune, width int) string {
	if width <= 0 {
		return ""
	}
	if len(runes) <= width {
		return string(runes)
	}
	if width == 1 {
		return "…"
	}
	return string(runes[:width-1]) + "…"
}

func max(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func truncateToWidth(s string, width int) string {
	if width <= 0 {
		return ""
	}
	r := []rune(s)
	if len(r) <= width {
		return s
	}
	if width == 1 {
		return "."
	}
	return string(r[:width-1]) + "."
}

func modeLabel(v mode) string {
	switch v {
	case modeZen:
		return "zen"
	case modePreview:
		return "preview"
	}
	return "split"
}

func onOff(enabled bool) string {
	if enabled {
		return "on"
	}
	return "off"
}

func labelForPanel(v panel) string {
	switch v {
	case panelEditor:
		return "editor"
	case panelPreview:
		return "preview"
	}
	return "browser"
}
