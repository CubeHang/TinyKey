#!/usr/bin/env pwsh
#
# TinyKey 宿主机测试
#
# 本工程用的是 Keil MDK（armcc/armclang），本机没有 ARM 工具链，
# 因此用 MinGW gcc 把**与目标完全相同的源码**在电脑上编译并运行：
#   kb_logic_test : 真 Core/Src/keyboard.c + 测试内置的假移植层
#   kb_port_test  : 真 keyboard.c + 真 keyboard_port.c + 假 HAL/假 USB（带二极管矩阵模型）
#   hid_desc_test : 真 usbd_hid.c + 捕获型 USB 桩（逐字节校验描述符）
#
# 用法：pwsh -File tests/run_tests.ps1

$ErrorActionPreference = 'Stop'

$testsDir = $PSScriptRoot
$root = Split-Path -Parent $testsDir
$buildDir = Join-Path $testsDir 'build'
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$gccCmd = Get-Command gcc -ErrorAction SilentlyContinue
if ($null -eq $gccCmd) {
  Write-Host '找不到 gcc：请安装 MinGW 并加入 PATH' -ForegroundColor Red
  exit 2
}
$gcc = $gccCmd.Path

$includeDirs = @(
  'tests/shims',
  'Core/Inc',
  'USB_DEVICE/App',
  'USB_DEVICE/Target',
  'Middlewares/ST/STM32_USB_Device_Library/Core/Inc',
  'Middlewares/ST/STM32_USB_Device_Library/Class/HID/Inc'
) | ForEach-Object { (Join-Path $root $_).Replace('\', '/') }

$flags = @('-std=c11', '-Wall', '-Wextra', '-Wno-unused-parameter', '-O1', '-g') +
         ($includeDirs | ForEach-Object { "-I$_" })

$suites = [ordered]@{
  'kb_logic_test' = @(
    'tests/kb_logic_test.c',
    'Core/Src/keyboard.c',
    'tests/shims/test_util.c'
  )
  'kb_port_test' = @(
    'tests/kb_port_test.c',
    'Core/Src/keyboard.c',
    'Core/Src/keyboard_port.c',
    'tests/shims/fake_hal.c',
    'tests/shims/fake_usb_report.c',
    'tests/shims/test_util.c'
  )
  'hid_desc_test' = @(
    'tests/hid_desc_test.c',
    'Middlewares/ST/STM32_USB_Device_Library/Class/HID/Src/usbd_hid.c',
    'tests/shims/fake_usb_desc.c',
    'tests/shims/test_util.c'
  )
}

Write-Host "gcc       : $gcc" -ForegroundColor DarkGray
Write-Host "工程根目录: $root" -ForegroundColor DarkGray

$failed = New-Object System.Collections.Generic.List[string]
$compileFailed = New-Object System.Collections.Generic.List[string]

foreach ($name in $suites.Keys) {
  $sources = $suites[$name] | ForEach-Object { (Join-Path $root $_).Replace('\', '/') }
  $exe = Join-Path $buildDir "$name.exe"

  Write-Host "`n>>> 编译 $name" -ForegroundColor Cyan
  & $gcc @flags @sources '-o' $exe
  if ($LASTEXITCODE -ne 0) {
    $compileFailed.Add($name)
    continue
  }

  & $exe
  if ($LASTEXITCODE -ne 0) {
    $failed.Add($name)
  }
}

Write-Host ''
if (($compileFailed.Count -gt 0) -or ($failed.Count -gt 0)) {
  if ($compileFailed.Count -gt 0) {
    Write-Host "编译失败: $($compileFailed -join ', ')" -ForegroundColor Red
  }
  if ($failed.Count -gt 0) {
    Write-Host "用例失败: $($failed -join ', ')" -ForegroundColor Red
  }
  exit 1
}

Write-Host '全部测试通过' -ForegroundColor Green
exit 0
