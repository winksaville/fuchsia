// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
package main

import (
	"encoding/json"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path"
	"reflect"
	"strings"
	"testing"
)

func Test_formatSize(t *testing.T) {
	tests := []struct {
		size     int64
		expected string
	}{
		{
			0, "0.00 bytes",
		},
		{
			1024, "1.00 KiB",
		},
		{
			1024 * 1024, "1.00 MiB",
		},
		{
			1024 * 1024 * 1024, "1.00 GiB",
		},
	}

	for _, test := range tests {
		if result := formatSize(test.size); result != test.expected {
			t.Errorf("formatSize(%d) = %s; expect %s", test.size, result, test.expected)
		}
	}
}
func Test_processSizes(t *testing.T) {
	tests := []struct {
		name     string
		file     io.Reader
		expected map[string]int64
	}{
		{
			"One Line", strings.NewReader("foo=0"), map[string]int64{"foo": 0},
		},
		{
			"Two Lines", strings.NewReader("foo=1\nbar=2"), map[string]int64{"foo": 1, "bar": 2},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if m := processSizes(test.file); !reflect.DeepEqual(m, test.expected) {
				t.Fatalf("processSizes(%s) = %+v; expect %+v", test.file, m, test.expected)
			}
		})
	}
}
func Test_processManifest(t *testing.T) {
	tests := []struct {
		name            string
		blobMap         map[string]*Blob
		sizeMap         map[string]int64
		fileName        string
		file            io.Reader
		expectedPackage []string
		expectedBlobMap map[string]*Blob
	}{
		{
			"Adding New Blob To Empty Map",
			map[string]*Blob{},
			map[string]int64{"hash": 1},
			"fileFoo",
			strings.NewReader("hash=foo"),
			[]string{},
			map[string]*Blob{"hash": {dep: []string{"fileFoo"}, size: 1, name: "foo", hash: "hash"}},
		},
		{
			"Adding New Meta Far To Empty Map",
			map[string]*Blob{},
			map[string]int64{"hash": 1},
			"fileFoo",
			strings.NewReader("hash=meta.far"),
			[]string{"meta.far"},
			map[string]*Blob{"hash": {dep: []string{"fileFoo"}, size: 1, name: "meta.far", hash: "hash"}},
		},
		{
			"Adding A Blob To A Map With That Blob",
			map[string]*Blob{"hash": {dep: []string{"foo"}, size: 1, name: "foo", hash: "hash"}},
			map[string]int64{"hash": 1},
			"fileFoo",
			strings.NewReader("hash=foo"),
			[]string{},
			map[string]*Blob{"hash": {dep: []string{"foo", "fileFoo"}, size: 1, name: "foo", hash: "hash"}},
		},
		{
			"Adding A Meta Far To A Map With That Meta Far",
			map[string]*Blob{"hash": {dep: []string{"foo"}, size: 1, name: "meta.far", hash: "hash"}},
			map[string]int64{"hash": 1},
			"fileFoo",
			strings.NewReader("hash=meta.far"),
			[]string{},
			map[string]*Blob{"hash": {dep: []string{"foo", "fileFoo"}, size: 1, name: "meta.far", hash: "hash"}},
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if p := processManifest(test.blobMap, test.sizeMap, test.fileName, test.file); !reflect.DeepEqual(p, test.expectedPackage) {
				t.Fatalf("processManifest(%+v, %+v, %s, %+v) = %+v; expect %+v", test.blobMap, test.sizeMap, test.fileName, test.file, p, test.expectedPackage)
			}

			if !reflect.DeepEqual(test.blobMap["hash"], test.expectedBlobMap["hash"]) {
				t.Fatalf("blob map: %+v; expect %+v", test.blobMap["hash"], test.expectedBlobMap["hash"])
			}
		})
	}
}
func Test_processBlobsJSON(t *testing.T) {
	tests := []struct {
		name            string
		blobMap         map[string]*Blob
		assetMap        map[string]bool
		assetSize       int64
		blobs           []BlobFromJSON
		expectedBlobMap map[string]*Blob
		expectedSize    int64
	}{
		{
			"Adding Asset Blob",
			map[string]*Blob{"hash": {size: 1}},
			map[string]bool{".asset": true},
			0,
			[]BlobFromJSON{{Path: "test.asset", Merkle: "hash"}},
			map[string]*Blob{},
			1,
		},
		{
			"Adding Non-asset Blob",
			map[string]*Blob{"hash": {size: 1, dep: []string{"not used"}}},
			map[string]bool{".asset": true},
			0,
			[]BlobFromJSON{{Path: "test.notasset", Merkle: "hash"}},
			map[string]*Blob{"hash": {size: 1, dep: []string{"not used"}}},
			0,
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			processBlobsJSON(test.blobMap, test.assetMap, &test.assetSize, test.blobs, newDummyNode(), "")

			if !reflect.DeepEqual(test.blobMap, test.expectedBlobMap) {
				t.Fatalf("blob map: %v; expect %v", test.blobMap, test.expectedBlobMap)
			}

			if test.assetSize != test.expectedSize {
				t.Fatalf("asset size: %d; expect %d", test.assetSize, test.expectedSize)
			}
		})
	}
}
func Test_checkLimit(t *testing.T) {
	tests := []struct {
		name     string
		size     int64
		limit    json.Number
		expected string
	}{
		{
			"Size Smaller Than Limit",
			1,
			json.Number("2"),
			"",
		},
		{
			"Size Equal To Limit",
			2,
			json.Number("2"),
			"",
		},
		{
			"Size Greater Than Limit",
			3,
			json.Number("2"),
			"foo (3.00 bytes) has exceeded its limit of 2.00 bytes.",
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if result := checkLimit("foo", test.size, test.limit); result != test.expected {
				t.Fatalf("checkLimit(foo, %d, %s) = %s; expect %s", test.size, test.limit, result, test.expected)
			}
		})
	}
}
func Test_nodeAdd(t *testing.T) {
	root := newDummyNode()
	testBlob := Blob{
		dep:  []string{"not used"},
		size: 10,
	}

	// Test adding a single node
	root.add("foo", &testBlob)
	child := root.find("foo")
	if child == nil {
		t.Fatal("foo is not added as the child of the root")
	}
	if child.size != 10 {
		t.Fatalf("the size of the foo node (root's child) is %d; expect 10", child.size)
	}

	// Test adding a node that shares a common path
	root.add("foo/bar", &testBlob)
	grandchild := root.find("foo/bar")
	if grandchild == nil {
		t.Fatal("bar is not added as the grandchild of the root")
	}
	if child.size != 20 {
		t.Fatalf("the size of the foo node (root's child) is %d; expect 20", child.size)
	}
	if grandchild.size != 10 {
		t.Fatalf("the size of the bar node (root's grandchild) is %d; expect 10", grandchild.size)
	}
}
func Test_nodeFind(t *testing.T) {
	tests := []struct {
		name     string
		path     string
		expected *Node
	}{
		{
			"Find Existing Node",
			"foo",
			&Node{"foo", 10, make(map[string]*Node)},
		},
		{
			"Find Nonexistent Node",
			"NONEXISTENT",
			nil,
		},
	}
	root := newDummyNode()
	root.add("foo", &Blob{dep: []string{"not used"}, size: 10})

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			if node := root.find(test.path); !reflect.DeepEqual(node, test.expected) {
				t.Fatalf("node.find(%s) = %+v; expect %+v", test.path, node, test.expected)
			}
		})
	}
}

func Test_processInput(t *testing.T) {
	fooSrcRelPath := "foo.src"
	input := Input{
		AssetLimit: json.Number("1"),
		CoreLimit:  json.Number("1"),
		AssetExt:   []string{".txt"},
		Components: []Component{
			Component{
				Component: "foo",
				Limit:     json.Number("1"),
				Src:       []string{"foo-pkg"},
			},
		},
	}
	buildDir, err := ioutil.TempDir("", "out")
	if err != nil {
		t.Fatalf("Failed to create build dir: %v", err)
	}
	defer os.RemoveAll(buildDir)
	pkgDir := path.Join(buildDir, "foo-pkg")
	if err := os.Mkdir(pkgDir, 0777); err != nil {

	}
	blobsJSONF, err := os.Create(path.Join(pkgDir, BlobsJSON))
	if err != nil {
		t.Fatalf("Failed to create %s: %v", BlobsJSON, err)
	}
	metaFarRelPath := path.Join("foo-pkg", MetaFar)
	blobs := []BlobFromJSON{
		BlobFromJSON{
			Merkle:     "deadbeef",
			Path:       "meta/",
			SourcePath: metaFarRelPath,
		},
		BlobFromJSON{
			Merkle:     "abc123",
			Path:       fooSrcRelPath,
			SourcePath: fooSrcRelPath,
		},
	}
	blobsJSONBytes, err := json.Marshal(blobs)
	if err != nil {
		t.Fatalf("Failed to marshal JSON for %s: %v", BlobsJSON, err)
	}
	if _, err := blobsJSONF.Write(blobsJSONBytes); err != nil {
		t.Fatalf("Failed to write %s: %v", BlobsJSON, err)
	}
	blobsJSONF.Close()
	blobManifestRelPath := "foo-pkg/blobs.manifest"
	blobManifestF, err := os.Create(path.Join(buildDir, blobManifestRelPath))
	if err != nil {
		t.Fatalf("Failed to create blob manifest file: %v", err)
	}
	blobManifest := fmt.Sprintf("deadbeef=%s\nabc123=%s\n", metaFarRelPath, fooSrcRelPath)
	if _, err := blobManifestF.Write([]byte(blobManifest)); err != nil {
		t.Fatalf("Failed to write blob manifest: %v", err)
	}
	blobManifestF.Close()
	blobListRelPath := "blob.manifest.list"
	blobListF, err := os.Create(path.Join(buildDir, blobListRelPath))
	if err != nil {
		t.Fatalf("Failed to create blob list file: %v", err)
	}
	if _, err := blobListF.Write([]byte(blobManifestRelPath)); err != nil {
		t.Fatalf("Failed to write blob list: %v", err)
	}
	blobListF.Close()
	blobSizeRelPath := "blob.sizes"
	blobSizeF, err := os.Create(path.Join(buildDir, blobSizeRelPath))
	if err != nil {
		t.Fatalf("Failed to create blob size file: %v", err)
	}
	const singleBlobSize = 4096
	if _, err := blobSizeF.Write([]byte(fmt.Sprintf("deadbeef=%d\nabc123=%d\n", singleBlobSize, singleBlobSize))); err != nil {
		t.Fatalf("Failed to write blob sizes: %v", err)
	}
	blobSizeF.Close()
	sizes, err := processInput(&input, buildDir, blobListRelPath, blobSizeRelPath)
	if err == nil {
		t.Fatalf("Expected processInput to return an error because size is above limit")
	} else if !strings.Contains(err.Error(), "foo") {
		t.Fatalf("Expected error message to mention component name \"foo\". Actual error: %v", err)
	} else {
		t.Logf("Error returned from processInput (probably expected): %v", err)
	}
	fooSize, ok := sizes["foo"]
	if !ok {
		t.Fatalf("Failed to find foo in sizes: %v", sizes)
	}
	if fooSize != int64(2*singleBlobSize) {
		t.Fatalf("Unexpected size for component foo: %v", fooSize)
	}
}
