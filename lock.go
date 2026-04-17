package main

import (
	"crypto/sha256"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
)

type lockStore struct {
	dir      string
	AppHash  string            `json:"app_hash"`
	Items    map[string]string `json:"items"`
}

func newLockStore(rootDir string) *lockStore {
	ls := &lockStore{
		dir:   rootDir,
		Items: make(map[string]string),
	}
	ls.load()
	return ls
}

func (ls *lockStore) path() string {
	return filepath.Join(ls.dir, ".locks.json")
}

func (ls *lockStore) load() {
	data, err := os.ReadFile(ls.path())
	if err != nil {
		return
	}
	json.Unmarshal(data, ls)
	if ls.Items == nil {
		ls.Items = make(map[string]string)
	}
}

func (ls *lockStore) save() error {
	data, err := json.MarshalIndent(ls, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(ls.path(), data, 0o644)
}

func hashPassword(password string) string {
	h := sha256.Sum256([]byte(password))
	return fmt.Sprintf("%x", h)
}

func (ls *lockStore) IsAppLocked() bool {
	return ls.AppHash != ""
}

func (ls *lockStore) SetAppLock(password string) error {
	ls.AppHash = hashPassword(password)
	return ls.save()
}

func (ls *lockStore) CheckAppPassword(password string) bool {
	return ls.AppHash == hashPassword(password)
}

func (ls *lockStore) RemoveAppLock() error {
	ls.AppHash = ""
	return ls.save()
}

func (ls *lockStore) IsItemLocked(itemPath string) bool {
	_, ok := ls.Items[itemPath]
	return ok
}

func (ls *lockStore) LockItem(itemPath, password string) error {
	ls.Items[itemPath] = hashPassword(password)
	return ls.save()
}

func (ls *lockStore) CheckItemPassword(itemPath, password string) bool {
	hash, ok := ls.Items[itemPath]
	if !ok {
		return true
	}
	return hash == hashPassword(password)
}

func (ls *lockStore) UnlockItem(itemPath string) error {
	delete(ls.Items, itemPath)
	return ls.save()
}
