/// The different types of audio filters.
enum FilterType {
  /// A biquad resonant filter.
  biquadResonantFilter,

  /// An equalizer filter.
  eqFilter,

  /// An echo filter.
  echoFilter,

  /// A lo-fi filter.
  lofiFilter,

  /// A flanger filter.
  flangerFilter,

  /// A bass-boost filter.
  bassboostFilter,

  /// A wave shaper filter.
  waveShaperFilter,

  /// A robotize filter.
  robotizeFilter,

  /// A reverb filter.
  freeverbFilter,
}

/// The parameters for each filter.
typedef FxParams = ({
  String title,
  List<String> names,
  List<double> mins,
  List<double> maxs,
  List<double> defs,
});

/// Biquad Resonant filter
const FxParams fxBiquadResonant = (
  title: 'Biquad Resonant',

  /// [Type] type is int
  names: ['Wet', 'Type', 'Frequency', 'Resonance'],
  mins: [0, 0.999, 10, 0.1],
  maxs: [1, 1.1, 16000, 20],
  defs: [1, 0.999, 0.5, 0.1],
);

/// EQ filter
const FxParams fxEq = (
  title: 'Equalizer',
  names: [
    'Wet',
    'Band 1',
    'Band 2',
    'Band 3',
    'Band 4',
    'Band 5',
    'Band 6',
    'Band 7',
    'Band 8',
  ],
  mins: [0, 0, 0, 0, 0, 0, 0, 0, 0],
  maxs: [1, 4, 4, 4, 4, 4, 4, 4, 4],
  defs: [1, 1, 1, 1, 1, 1, 1, 1, 1],
);

/// Echo filter
const FxParams fxEcho = (
  title: 'Echo',
  names: ['Wet', 'Delay', 'Decay', 'Filter'],
  mins: [0, 0.001, 0.001, 0],
  maxs: [1, double.maxFinite, 1, 1],
  defs: [1, 0.3, 0.7, 0],
);

/// Lo-fi filter
const FxParams fxLofi = (
  title: 'Lofi',
  names: ['Wet', 'Samplerate', 'Bitdepth'],
  mins: [0, 100, 0.5],
  maxs: [1, 22000, 16],
  defs: [1, 4000, 3],
);

/// Flanger filter
const FxParams fxFlanger = (
  title: 'Flanger',
  names: ['Wet', 'Delay', 'Freq'],
  mins: [0, 0.001, 0.001],
  maxs: [1, 0.1, 100],
  defs: [1, 0.005, 10],
);

/// Bass-boost filter
const FxParams fxBassboost = (
  title: 'Bassboost',
  names: ['Wet', 'Boost'],
  mins: [0, 0],
  maxs: [1, 10],
  defs: [1, 2],
);

/// WaveShaper filter
const FxParams fxWaveShaper = (
  title: 'Wave Shaper',
  names: ['Wet', 'Amount'],
  mins: [0, -1],
  maxs: [1, 1],
  defs: [1, 0],
);

/// Robotize filter
const FxParams fxRobotize = (
  title: 'Robotize',

  /// [Waveform] type is int
  names: ['Wet', 'Frequency', 'Waveform'],
  mins: [0, 0.1, 0],
  maxs: [1, 100, 6],
  defs: [1, 30, 0],
);

/// Freeverb (reverb) filter
const FxParams fxFreeverb = (
  title: 'Freeverb',

  /// [Freeze] type is bool
  names: ['Wet', 'Freeze', 'Room Size', 'Damp', 'Width'],
  mins: [0, 0, 0, 0, 0],
  maxs: [1, 1, 1, 1, 1],
  defs: [1, 0, 0.5, 0.5, 1],
);
