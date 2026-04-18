package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

type metaStore struct {
	dir string
	Pinned    map[string]bool   `json:"pinned"`
	Archived  map[string]bool   `json:"archived"`
	Recent    []recentEntry     `json:"recent"`
	Scratch   string            `json:"scratch"`
	Templates map[string]string `json:"templates"`
}

type recentEntry struct {
	Path string    `json:"path"`
	Name string    `json:"name"`
	At   time.Time `json:"at"`
}

func newMetaStore(rootDir string) *metaStore {
	ms := &metaStore{
		dir:       rootDir,
		Pinned:    make(map[string]bool),
		Archived:  make(map[string]bool),
		Templates: make(map[string]string),
	}
	ms.load()
	return ms
}

func (ms *metaStore) path() string {
	return filepath.Join(ms.dir, ".meta.json")
}

func (ms *metaStore) load() {
	data, err := os.ReadFile(ms.path())
	if err != nil {
		return
	}
	json.Unmarshal(data, ms)
	if ms.Pinned == nil {
		ms.Pinned = make(map[string]bool)
	}
	if ms.Archived == nil {
		ms.Archived = make(map[string]bool)
	}
	if ms.Templates == nil {
		ms.Templates = make(map[string]string)
	}
}

func (ms *metaStore) save() error {
	data, err := json.MarshalIndent(ms, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(ms.path(), data, 0o644)
}

func (ms *metaStore) TogglePin(notePath string) bool {
	ms.Pinned[notePath] = !ms.Pinned[notePath]
	if !ms.Pinned[notePath] {
		delete(ms.Pinned, notePath)
	}
	ms.save()
	return ms.Pinned[notePath]
}

func (ms *metaStore) IsPinned(notePath string) bool {
	return ms.Pinned[notePath]
}

func (ms *metaStore) ToggleArchive(notePath string) bool {
	ms.Archived[notePath] = !ms.Archived[notePath]
	if !ms.Archived[notePath] {
		delete(ms.Archived, notePath)
	}
	ms.save()
	return ms.Archived[notePath]
}

func (ms *metaStore) IsArchived(notePath string) bool {
	return ms.Archived[notePath]
}

func (ms *metaStore) TouchRecent(notePath, name string) {
	for i, r := range ms.Recent {
		if r.Path == notePath {
			ms.Recent = append(ms.Recent[:i], ms.Recent[i+1:]...)
			break
		}
	}
	ms.Recent = append([]recentEntry{{Path: notePath, Name: name, At: time.Now()}}, ms.Recent...)
	if len(ms.Recent) > 10 {
		ms.Recent = ms.Recent[:10]
	}
	ms.save()
}

func (ms *metaStore) GetRecent() []recentEntry {
	return ms.Recent
}

func (ms *metaStore) SaveScratch(content string) {
	ms.Scratch = content
	ms.save()
}

func (ms *metaStore) LoadScratch() string {
	return ms.Scratch
}

func (ms *metaStore) SetTemplate(name, content string) {
	ms.Templates[name] = content
	ms.save()
}

func (ms *metaStore) ListTemplates() []string {
	names := make([]string, 0, len(ms.Templates))
	for k := range ms.Templates {
		names = append(names, k)
	}
	sort.Strings(names)
	return names
}

func (ms *metaStore) GetTemplate(name string) (string, bool) {
	t, ok := ms.Templates[name]
	return t, ok
}

func (ms *metaStore) DeleteTemplate(name string) {
	delete(ms.Templates, name)
	ms.save()
}

func expandTemplate(tmpl, title string) string {
	now := time.Now()
	r := strings.NewReplacer(
		"{{date}}", now.Format("2006-01-02"),
		"{{time}}", now.Format("15:04"),
		"{{datetime}}", now.Format("2006-01-02 15:04"),
		"{{title}}", title,
		"{{year}}", now.Format("2006"),
		"{{month}}", now.Format("January"),
		"{{day}}", now.Format("02"),
		"{{weekday}}", now.Format("Monday"),
	)
	return r.Replace(tmpl)
}
