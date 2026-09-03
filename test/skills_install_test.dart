import 'dart:io';
import 'package:flutter_test/flutter_test.dart';

import '../bin/skills.dart';

void main() {
  late Directory tempDir;
  late Uri skillsRoot;

  setUp(() {
    tempDir = Directory.systemTemp.createTempSync('soloud_skills_test_');
    skillsRoot = Directory.current.uri.resolve('skills/');
  });

  tearDown(() {
    if (tempDir.existsSync()) {
      tempDir.deleteSync(recursive: true);
    }
  });

  group('Agent Skills installer', () {
    test('discovers all bundled skills and defaults to .agents home', () async {
      final plan = await planSkillInstall(
        projectRoot: tempDir,
        skillsRoot: skillsRoot,
      );

      expect(plan.action, SkillInstallAction.install);
      expect(plan.homes, ['.agents/skills']);
      expect(plan.skillNames.length, 14);
      expect(plan.installCount, 14);
      expect(plan.updateCount, 0);
      expect(plan.skillNames, contains('flutter-soloud-idioms'));
      expect(plan.skillNames, contains('flutter-soloud-setup'));
    });

    test(
      'installs all skills into default .agents/skills and is idempotent',
      () async {
        final result = await installSkills(
          projectRoot: tempDir,
          skillsRoot: skillsRoot,
        );

        expect(
          result,
          contains(
            'Installed 14 flutter_soloud agent skills into .agents/skills.',
          ),
        );

        for (final skillName in [
          'flutter-soloud-3d-audio',
          'flutter-soloud-filters',
          'flutter-soloud-idioms',
          'flutter-soloud-loading',
          'flutter-soloud-mixing-bus',
          'flutter-soloud-output-capture',
          'flutter-soloud-playback',
          'flutter-soloud-pull-streaming',
          'flutter-soloud-scheduling',
          'flutter-soloud-setup',
          'flutter-soloud-streaming',
          'flutter-soloud-synthesis',
          'flutter-soloud-visualization',
          'flutter-soloud-volume-pan',
        ]) {
          final skillFile = File.fromUri(
            tempDir.uri.resolve('.agents/skills/$skillName/SKILL.md'),
          );
          expect(
            skillFile.existsSync(),
            isTrue,
            reason: '$skillName/SKILL.md should exist',
          );
        }

        // References should also be copied
        final webRef = File.fromUri(
          tempDir.uri.resolve(
            '.agents/skills/flutter-soloud-setup/references/web.md',
          ),
        );
        expect(webRef.existsSync(), isTrue);

        // Subsequent plan should be upToDate
        final planAfter = await planSkillInstall(
          projectRoot: tempDir,
          skillsRoot: skillsRoot,
        );
        expect(planAfter.action, SkillInstallAction.upToDate);
        expect(planAfter.installCount, 0);
        expect(planAfter.updateCount, 0);
      },
    );

    test('installs into all present agent homes', () async {
      // Create .claude and .cursor directories in the project root
      Directory.fromUri(tempDir.uri.resolve('.claude/')).createSync();
      Directory.fromUri(tempDir.uri.resolve('.cursor/')).createSync();

      final plan = await planSkillInstall(
        projectRoot: tempDir,
        skillsRoot: skillsRoot,
      );

      expect(plan.homes, ['.claude/skills', '.cursor/skills']);

      await installSkills(projectRoot: tempDir, skillsRoot: skillsRoot);

      final claudeSkill = File.fromUri(
        tempDir.uri.resolve('.claude/skills/flutter-soloud-idioms/SKILL.md'),
      );
      final cursorSkill = File.fromUri(
        tempDir.uri.resolve('.cursor/skills/flutter-soloud-idioms/SKILL.md'),
      );
      expect(claudeSkill.existsSync(), isTrue);
      expect(cursorSkill.existsSync(), isTrue);

      // .agents should not be created when other agent homes exist
      final agentsDir = Directory.fromUri(tempDir.uri.resolve('.agents/'));
      expect(agentsDir.existsSync(), isFalse);
    });

    test('detects stale versions and updates them', () async {
      await installSkills(projectRoot: tempDir, skillsRoot: skillsRoot);

      // Modify one installed skill to have version: 0
      final file = File.fromUri(
        tempDir.uri.resolve('.agents/skills/flutter-soloud-idioms/SKILL.md'),
      );
      final content = file.readAsStringSync().replaceFirst(
        'version: 1',
        'version: 0',
      );
      file.writeAsStringSync(content);

      final plan = await planSkillInstall(
        projectRoot: tempDir,
        skillsRoot: skillsRoot,
      );

      expect(plan.action, SkillInstallAction.update);
      expect(plan.updateCount, 1);
      expect(
        describeSkillPlan(plan),
        contains(
          '1 flutter_soloud agent skill(s) have a newer version available',
        ),
      );

      // Re-installing updates it
      await installSkills(projectRoot: tempDir, skillsRoot: skillsRoot);

      final planAfter = await planSkillInstall(
        projectRoot: tempDir,
        skillsRoot: skillsRoot,
      );
      expect(planAfter.action, SkillInstallAction.upToDate);
    });

    test('CLI --check exit code reflects installation state', () async {
      final binScript = Directory.current.uri
          .resolve('bin/skills.dart')
          .toFilePath();

      // Check on uninstalled tempDir -> exit code 1
      final resultBefore = Process.runSync(Platform.executable, [
        binScript,
        '--check',
        '--project-root',
        tempDir.path,
      ]);
      expect(resultBefore.exitCode, 1);
      expect(resultBefore.stdout.toString(), contains('are not installed'));

      // Run installation CLI
      final installResult = Process.runSync(Platform.executable, [
        binScript,
        '--project-root',
        tempDir.path,
      ]);
      expect(installResult.exitCode, 0);
      expect(
        installResult.stdout.toString(),
        contains('Installed 14 flutter_soloud agent skills'),
      );

      // Check on installed tempDir -> exit code 0
      final resultAfter = Process.runSync(Platform.executable, [
        binScript,
        '--check',
        '--project-root',
        tempDir.path,
      ]);
      expect(resultAfter.exitCode, 0);
      expect(
        resultAfter.stdout.toString(),
        contains(
          'The flutter_soloud agent skills are up to date (14 installed).',
        ),
      );
    });
  });
}
