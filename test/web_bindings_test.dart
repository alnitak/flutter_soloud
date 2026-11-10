// Browser tests of how the web bindings use the WASM heap, against a
// stand-in for the module (Module_soloud) that keeps track of its
// allocations. Run with:
//
//   flutter test --platform chrome test/web_bindings_test.dart
@TestOn('browser')
library;

import 'dart:js_interop';
import 'dart:js_interop_unsafe';

import 'package:flutter_soloud/src/bindings/bindings_player_web.dart';
import 'package:flutter_soloud/src/enums.dart';
import 'package:flutter_soloud/src/filters/filters.dart';
import 'package:flutter_test/flutter_test.dart';

/// A stand-in for the module. Its allocator reuses a freed block for the
/// next allocation that fits, as the module's does, and fills freed memory
/// with 0xff bytes (a NaN as a float). `getFilterParamNames` gives the
/// parametric EQ 195 parameters named `Param 0`, `Param 1`, ... and every
/// other filter 4, like the module does with malloc()'d strings.
/// `UTF8ToString` throws for the string that `unreadable` is set to.
const _standIn = '''
(function () {
  const heap = new ArrayBuffer(1 << 20);
  const HEAPU8 = new Uint8Array(heap);
  const HEAP32 = new Int32Array(heap);
  const HEAPF32 = new Float32Array(heap);
  const live = new Map();
  const freed = [];
  let next = 16;
  const m = {
    HEAPU8: HEAPU8,
    HEAPF32: HEAPF32,
    overflow: false,
    badFree: false,
    unreadable: null,
    liveAllocations: function () { return live.size; },
    _malloc: function (n) {
      const size = (n + 15) & ~15;
      for (let i = 0; i < freed.length; i++) {
        if (freed[i].size >= size) {
          const p = freed.splice(i, 1)[0].ptr;
          live.set(p, n);
          return p;
        }
      }
      const p = next;
      next += size;
      live.set(p, n);
      return p;
    },
    _free: function (p) {
      if (p === 0) return;
      const n = live.get(p);
      if (n === undefined) { m.badFree = true; return; }
      live.delete(p);
      HEAPU8.fill(0xff, p, p + n);
      freed.push({ ptr: p, size: (n + 15) & ~15 });
    },
    getValue: function (p, type) {
      if (type === 'i32') return HEAP32[p >> 2];
      if (type === 'float') return HEAPF32[p >> 2];
      throw new Error('getValue: ' + type);
    },
    UTF8ToString: function (p) {
      let s = '';
      while (HEAPU8[p] !== 0) s += String.fromCharCode(HEAPU8[p++]);
      if (s === m.unreadable) throw new Error('cannot read ' + s);
      return s;
    },
    _getFilterParamNames: function (filterType, countPtr, namesPtr) {
      const count = filterType === 11 ? 195 : 4;
      if (live.get(namesPtr) < count * 4) m.overflow = true;
      HEAP32[countPtr >> 2] = count;
      for (let i = 0; i < count; i++) {
        const name = 'Param ' + i;
        const p = m._malloc(name.length + 1);
        for (let j = 0; j < name.length; j++) HEAPU8[p + j] = name.charCodeAt(j);
        HEAPU8[p + name.length] = 0;
        HEAP32[(namesPtr >> 2) + i] = p;
      }
      return 0;
    },
  };
  globalThis.Module_soloud = m;
})();
''';

JSObject get _module => globalContext['Module_soloud']! as JSObject;

int get _liveAllocations =>
    _module.callMethod<JSNumber>('liveAllocations'.toJS).toDartInt;

bool _flag(String name) => (_module[name]! as JSBoolean).toDart;

void main() {
  setUp(() {
    globalContext.callMethod<JSAny?>('eval'.toJS, _standIn.toJS);
  });

  test('getFilterParamNames reads every name and frees it', () {
    final ret = FlutterSoLoudWeb().getFilterParamNames(FilterType.parametricEq);

    expect(ret.error, PlayerErrors.noError);
    expect(ret.names, [for (var i = 0; i < 195; i++) 'Param $i']);
    expect(_flag('overflow'), isFalse, reason: 'the names array is too small');
    expect(_flag('badFree'), isFalse);
    expect(_liveAllocations, 0, reason: 'heap memory is leaked');
  });

  test('getFilterParamNames frees every name when one cannot be read', () {
    _module['unreadable'] = 'Param 7'.toJS;

    expect(
      () => FlutterSoLoudWeb().getFilterParamNames(FilterType.parametricEq),
      throwsA(anything),
    );
    expect(_flag('badFree'), isFalse);
    expect(_liveAllocations, 0, reason: 'heap memory is leaked');
  });
}
