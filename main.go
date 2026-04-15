package main

import (
	"fmt"
	"os"

	tea "github.com/charmbracelet/bubbletea"
)

func main() {
	storage, err := newStorage()
	if err != nil {
		fmt.Fprintf(os.Stderr, "blob: %v\n", err)
		os.Exit(1)
	}

	model, err := newModel(storage)
	if err != nil {
		fmt.Fprintf(os.Stderr, "blob: %v\n", err)
		os.Exit(1)
	}

	program := tea.NewProgram(model, tea.WithAltScreen())
	if _, err := program.Run(); err != nil {
		fmt.Fprintf(os.Stderr, "blob: %v\n", err)
		os.Exit(1)
	}
}
