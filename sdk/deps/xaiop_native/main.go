package main

/*
#include <stdlib.h>
*/
import "C"

import (
	"encoding/json"
	"sync"
	"sync/atomic"
	"unsafe"

	"github.com/AboutUip/XAIOP/xaiop-sdk/go/xaiop"
)

func main() {}

func setErr(errOut **C.char, msg string) {
	if errOut == nil {
		return
	}
	*errOut = C.CString(msg)
}

//export xaiop_native_free
func xaiop_native_free(p *C.char) {
	if p != nil {
		C.free(unsafe.Pointer(p))
	}
}

//export xaiop_native_sdk_version
func xaiop_native_sdk_version() *C.char {
	return C.CString(xaiop.SDKVersion)
}

//export xaiop_native_protocol_version
func xaiop_native_protocol_version() *C.char {
	return C.CString(xaiop.ProtocolVersion)
}

//export xaiop_native_parse_to_json
func xaiop_native_parse_to_json(wire *C.char, errOut **C.char) *C.char {
	if wire == nil {
		setErr(errOut, "wire is nil")
		return nil
	}
	parsed, err := xaiop.Parse(C.GoString(wire))
	if err != nil {
		setErr(errOut, err.Error())
		return nil
	}
	snap := xaiop.MaterializeSnapshot(parsed)
	b, err := json.Marshal(snap)
	if err != nil {
		setErr(errOut, err.Error())
		return nil
	}
	return C.CString(string(b))
}

//export xaiop_native_encode_json
func xaiop_native_encode_json(jsonText *C.char, errOut **C.char) *C.char {
	if jsonText == nil {
		setErr(errOut, "json is nil")
		return nil
	}
	var value any
	if err := json.Unmarshal([]byte(C.GoString(jsonText)), &value); err != nil {
		setErr(errOut, err.Error())
		return nil
	}
	wire, err := xaiop.Encode(value, xaiop.EncodeOptions{})
	if err != nil {
		setErr(errOut, err.Error())
		return nil
	}
	return C.CString(wire)
}

var (
	liveSeq atomic.Int64
	liveMu  sync.Mutex
	lives   = map[int64]*xaiop.LiveParser{}
)

//export xaiop_native_live_create
func xaiop_native_live_create() C.longlong {
	id := liveSeq.Add(1)
	liveMu.Lock()
	lives[id] = xaiop.NewLiveParser()
	liveMu.Unlock()
	return C.longlong(id)
}

//export xaiop_native_live_free
func xaiop_native_live_free(id C.longlong) {
	liveMu.Lock()
	delete(lives, int64(id))
	liveMu.Unlock()
}

//export xaiop_native_live_feed_text
func xaiop_native_live_feed_text(id C.longlong, text *C.char, errOut **C.char) C.int {
	liveMu.Lock()
	lp := lives[int64(id)]
	liveMu.Unlock()
	if lp == nil {
		setErr(errOut, "invalid live parser id")
		return 0
	}
	if text == nil {
		return 1
	}
	lp.FeedText(C.GoString(text))
	if _, err := lp.Value(); err != nil {
		setErr(errOut, err.Error())
		return 0
	}
	return 1
}

//export xaiop_native_live_snapshot_json
func xaiop_native_live_snapshot_json(id C.longlong, errOut **C.char) *C.char {
	liveMu.Lock()
	lp := lives[int64(id)]
	liveMu.Unlock()
	if lp == nil {
		setErr(errOut, "invalid live parser id")
		return nil
	}
	val, err := lp.Value()
	if err != nil {
		setErr(errOut, err.Error())
		return nil
	}
	snap := xaiop.MaterializeSnapshot(val)
	b, err := json.Marshal(snap)
	if err != nil {
		setErr(errOut, err.Error())
		return nil
	}
	return C.CString(string(b))
}
