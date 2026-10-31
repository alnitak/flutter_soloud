import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';
import 'package:native_toolchain_c/native_toolchain_c.dart';

const String _libName = 'custom_dsp_callbacks';
const String _assetName = 'src/custom_dsp.c';

void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;

    final builder = CBuilder.library(
      name: _libName,
      assetName: _assetName,
      sources: [
        'src/custom_dsp.c',
      ],
      includes: [
        'src',
      ],
    );

    await builder.run(
      input: input,
      output: output,
    );
  });
}
