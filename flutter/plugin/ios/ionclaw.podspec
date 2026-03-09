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
  s.dependency 'Flutter'
  s.frameworks = 'Foundation'

  s.pod_target_xcconfig = { 'DEFINES_MODULE' => 'YES', 'EXCLUDED_ARCHS[sdk=iphonesimulator*]' => 'i386' }
  s.swift_version = '5.0'
  s.ios.deployment_target = '13.0'

  s.ios.vendored_frameworks = 'ionclaw.xcframework'
end
