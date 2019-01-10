package testrunner

import (
	"context"
	"io"
	"strings"

	"golang.org/x/crypto/ssh"
)

// SSHRunner runs commands over SSH.
type SSHRunner struct {
	Session *ssh.Session
}

// Run executes the given command.
func (r *SSHRunner) Run(ctx context.Context, command []string, stdout io.Writer, stderr io.Writer) error {
	r.Session.Stdout = stdout
	r.Session.Stderr = stderr
	cmd := strings.Join(command, " ")

	if err := r.Session.Start(cmd); err != nil {
		return err
	}

	done := make(chan error)
	go func() {
		done <- r.Session.Wait()
	}()

	select {
	case err := <-done:
		return err
	case <-ctx.Done():
		r.Session.Signal(ssh.SIGKILL)
	}

	return nil
}
