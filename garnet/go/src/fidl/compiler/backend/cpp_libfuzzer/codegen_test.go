// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package cpp_libfuzzer

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"fidl/compiler/backend/cpp/ir"
	"fidl/compiler/backend/typestest"
)

// basePath holds the base path to the directory containing goldens.
var basePath = func() string {
	testPath, err := filepath.Abs(os.Args[0])
	if err != nil {
		panic(err)
	}
	testDataDir := filepath.Join(filepath.Dir(testPath), "test_data", "fidlgen")
	return fmt.Sprintf("%s%c", testDataDir, filepath.Separator)
}()

type example string

func (s example) primaryHeader() string {
	return fmt.Sprintf("%s.h", s)
}

func (s example) goldenHeader() string {
	return fmt.Sprintf("%s.libfuzzer.h.golden", s)
}

func (s example) goldenSource() string {
	return fmt.Sprintf("%s.libfuzzer.cc.golden", s)
}

func TestCodegenHeader(t *testing.T) {
	for _, filename := range typestest.AllExamples(basePath) {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(basePath, filename)
			tree := ir.Compile(fidl)
			prepareTree(fidl.Name, &tree)
			header := typestest.GetGolden(basePath, example(filename).goldenHeader())

			buf := new(bytes.Buffer)
			if err := NewFidlGenerator().GenerateHeader(buf, tree); err != nil {
				t.Fatalf("unexpected error while generating header: %s", err)
			}

			typestest.AssertCodegenCmp(t, header, buf.Bytes())
		})
	}
}

func TestCodegenSource(t *testing.T) {
	for _, filename := range typestest.AllExamples(basePath) {
		t.Run(filename, func(t *testing.T) {
			fidl := typestest.GetExample(basePath, filename)
			tree := ir.Compile(fidl)
			prepareTree(fidl.Name, &tree)
			source := typestest.GetGolden(basePath, example(filename).goldenSource())

			buf := new(bytes.Buffer)
			if err := NewFidlGenerator().GenerateSource(buf, tree); err != nil {
				t.Fatalf("unexpected error while generating source: %s", err)
			}

			typestest.AssertCodegenCmp(t, source, buf.Bytes())
		})
	}
}