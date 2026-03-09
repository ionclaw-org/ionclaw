# frozen_string_literal: true

Pod::Spec.new do |s|
  s.name             = 'ionclaw'
  s.version          = '1.0.0'
  s.summary          = 'IonClaw Flutter Plugin.'
  s.description      = 'IonClaw AI Agent Orchestrator - Native FFI Plugin.'
  s.homepage         = 'https://github.com/niclaw/ionclaw'
  s.license          = { file: '../LICENSE' }
  s.author           = { 'Paulo Coutinho' => 'paulocoutinhox@gmail.com' }

  s.source = { path: '.' }
  s.dependency 'FlutterMacOS'

  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES' }
  s.osx.deployment_target = '10.15'

  # pre-built shared library (symlink to build output)
  s.osx.vendored_libraries = 'libionclaw.dylib'
end
