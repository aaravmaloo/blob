package main

import (
	"errors"
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

const noteExtension = ".md"

type browserNode struct {
	name      string
	path      string
	isFolder  bool
	collapsed bool
	children  []*browserNode
	parent    *browserNode
}

type storage struct {
	rootDir string
}

func newStorage() (*storage, error) {
	home, err := os.UserHomeDir()
	if err != nil {
		return nil, err
	}

	rootDir := filepath.Join(home, ".blob")
	if err := os.MkdirAll(rootDir, 0o755); err != nil {
		return nil, err
	}

	return &storage{rootDir: rootDir}, nil
}

func (s *storage) loadTree() ([]*browserNode, error) {
	entries, err := os.ReadDir(s.rootDir)
	if err != nil {
		return nil, err
	}

	nodes := make([]*browserNode, 0, len(entries))
	for _, entry := range entries {
		node, err := s.loadEntry(s.rootDir, entry, nil)
		if err != nil {
			return nil, err
		}
		if node != nil {
			nodes = append(nodes, node)
		}
	}

	sortNodes(nodes)
	return nodes, nil
}

func (s *storage) loadEntry(base string, entry fs.DirEntry, parent *browserNode) (*browserNode, error) {
	name := entry.Name()
	if strings.HasPrefix(name, ".") {
		return nil, nil
	}

	fullPath := filepath.Join(base, name)
	if entry.IsDir() {
		childrenEntries, err := os.ReadDir(fullPath)
		if err != nil {
			return nil, err
		}

		node := &browserNode{
			name:      name,
			path:      fullPath,
			isFolder:  true,
			collapsed: false,
			parent:    parent,
		}

		for _, childEntry := range childrenEntries {
			child, err := s.loadEntry(fullPath, childEntry, node)
			if err != nil {
				return nil, err
			}
			if child != nil {
				node.children = append(node.children, child)
			}
		}
		sortNodes(node.children)
		return node, nil
	}

	if filepath.Ext(name) != noteExtension {
		return nil, nil
	}

	return &browserNode{
		name:     strings.TrimSuffix(name, noteExtension),
		path:     fullPath,
		isFolder: false,
		parent:   parent,
	}, nil
}

func sortNodes(nodes []*browserNode) {
	sort.Slice(nodes, func(i, j int) bool {
		if nodes[i].isFolder != nodes[j].isFolder {
			return nodes[i].isFolder
		}
		return strings.ToLower(nodes[i].name) < strings.ToLower(nodes[j].name)
	})
}

func (s *storage) ensureWelcomeNote() (string, error) {
	nodes, err := s.loadTree()
	if err != nil {
		return "", err
	}
	if len(nodes) > 0 {
		return "", nil
	}

	path := filepath.Join(s.rootDir, "welcome-to-blob"+noteExtension)
	content := strings.Join([]string{
		"# Welcome to BLOB",
		"",
		"BLOB keeps notes in your home directory under ~/.blob.",
		"",
		"- n creates a note (in browser panel)",
		"- N creates a folder (in browser panel)",
		"- t creates a note from a template",
		"- T saves the current note as a template",
		"- p or Tab swaps focus between browser and editor",
		"- z toggles zen fullscreen on the selected note",
		"- Shift+Enter or Ctrl+Enter are zen fallbacks",
		"- v toggles vim editing",
		"- H or Ctrl+H opens help",
		"- r renames the selected note or folder",
		"- d deletes the selected note or folder",
		"- s opens the scratch pad (press s again to save & close)",
		"- . pins/unpins a note to the top of the browser",
		"- a archives/unarchives a note (browser panel)",
		"- A toggles archived note visibility",
		"- Ctrl+K opens link finder to insert [[links]]",
		"- / searches notes",
		"",
		"Templates support variables: {{date}}, {{time}}, {{datetime}}, {{title}}, {{year}}, {{month}}, {{day}}, {{weekday}}",
		"",
		"Fallbacks also still available: Ctrl+N, Ctrl+Shift+N, Ctrl+Shift+P, Ctrl+Shift+F, Ctrl+Shift+V",
		"",
		"Press i to edit, Esc to return to normal mode.",
	}, "\n")

	return path, os.WriteFile(path, []byte(content), 0o644)
}

func (s *storage) createFolder(target *browserNode) (string, error) {
	parent := s.rootDir
	if target != nil && target.isFolder {
		parent = target.path
	}

	base := "New Folder"
	path, err := uniquePath(parent, base, "")
	if err != nil {
		return "", err
	}

	return path, os.Mkdir(path, 0o755)
}

func (s *storage) createNote(target *browserNode) (string, error) {
	parent := s.rootDir
	if target != nil && target.isFolder {
		parent = target.path
	}

	base := "Untitled Note"
	path, err := uniquePath(parent, base, noteExtension)
	if err != nil {
		return "", err
	}

	initial := "# " + strings.TrimSuffix(filepath.Base(path), noteExtension) + "\n\n"
	return path, os.WriteFile(path, []byte(initial), 0o644)
}

func (s *storage) readNote(path string) (string, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return "", err
	}
	return string(data), nil
}

func (s *storage) writeNote(path, content string) error {
	if path == "" {
		return errors.New("missing note path")
	}
	return os.WriteFile(path, []byte(content), 0o644)
}

func (s *storage) renameNode(path, displayName string, isFolder bool) (string, error) {
	if path == "" {
		return "", errors.New("missing path")
	}
	displayName = sanitizeName(displayName)
	if displayName == "" {
		return "", errors.New("name cannot be empty")
	}

	parent := filepath.Dir(path)
	ext := ""
	if !isFolder {
		ext = noteExtension
	}

	currentName := strings.TrimSuffix(filepath.Base(path), noteExtension)
	if isFolder {
		currentName = filepath.Base(path)
	}
	if displayName == currentName {
		return path, nil
	}

	nextPath, err := uniquePath(parent, displayName, ext)
	if err != nil {
		return "", err
	}
	if err := os.Rename(path, nextPath); err != nil {
		return "", err
	}
	return nextPath, nil
}

func (s *storage) deleteNote(path string) error {
	if path == "" {
		return errors.New("missing note path")
	}
	if filepath.Ext(path) != noteExtension {
		return errors.New("only notes can be deleted")
	}
	return os.Remove(path)
}

func (s *storage) deleteFolder(path string) error {
	if path == "" {
		return errors.New("missing folder path")
	}
	if path == s.rootDir {
		return errors.New("refusing to delete root storage folder")
	}
	return os.RemoveAll(path)
}

func uniquePath(dir, base, ext string) (string, error) {
	safeBase := sanitizeName(base)
	for i := 0; i < 5000; i++ {
		name := safeBase
		if i > 0 {
			name = fmt.Sprintf("%s %d", safeBase, i+1)
		}
		path := filepath.Join(dir, name+ext)
		if _, err := os.Stat(path); errors.Is(err, os.ErrNotExist) {
			return path, nil
		} else if err != nil {
			return "", err
		}
	}
	return "", errors.New("could not allocate unique path")
}

func sanitizeName(name string) string {
	replacer := strings.NewReplacer(
		"/", "-",
		"\\", "-",
		":", "-",
		"*", "",
		"?", "",
		"\"", "",
		"<", "",
		">", "",
		"|", "",
	)
	trimmed := strings.TrimSpace(replacer.Replace(name))
	if trimmed == "" {
		return "Untitled"
	}
	return trimmed
}
