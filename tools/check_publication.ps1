param()
$ErrorActionPreference = 'Stop'

# Audit the index, not the working copy. Report only path/category/line, never values.
$paths = @(& git -c core.quotepath=false diff --cached --name-only --diff-filter=ACMR)
if ($LASTEXITCODE -ne 0) { throw 'Cannot read staged paths' }
if (-not $paths.Count) { throw 'No staged files to audit' }

$allowedRoot = '^(?:\.gitignore|\.gitattributes|README\.md|GLOSSARY\.md|Makefile)$'
$allowedDirectory = '^(?:apps/light_monitor|drivers/(?:lightkey|lightap3216c|lightbeep)|device-tree|tools/(?:finalization|i2c-nack-test)|docs/validation)/'
$allowedExtra = '^(?:tools/(?:check_publication\.ps1|boot_lightkey\.ttl|boot_lightbeep\.ttl)|docs/(?:BUILD|HARDWARE|VALIDATION|PUBLISHING)\.md)$'
$forbidden = '(?i)(?:^|/)(?:\.codex|\.agents|\.aws|\.ssh|\.git|\.env(?:\..*)?|auth\.json|credentials(?:\..*)?|secrets(?:\..*)?|id_rsa[^/]*|id_ed25519[^/]*)(?:/|$)|\.(?:pem|key|p12|pfx|jks|keystore|ko|o|so|a|dtb|dtbo|zip|7z|csv|log|tar|gz)$'
$patterns = [ordered]@{
    ApiToken = '(?i)(?:sk-(?:proj-|svcacct-)?[a-z0-9_-]{16,}|gh[pousr]_[a-z0-9]{20,}|github_pat_[a-z0-9_]{20,}|AKIA[0-9A-Z]{16})'
    PrivateKey = '-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----'
    CredentialAssignment = '(?im)(?:api[_-]?key|access[_-]?token|auth[_-]?token|client[_-]?secret|password|passwd)\s*[:=]\s*["''][^"''\r\n]{6,}["'']'
    AuthHeader = '(?i)(?:authorization\s*[:=]\s*(?:bearer|basic)\s+\S+|https?://[^\s/:]+:[^\s/@]+@)'
}
$findings = @()
foreach ($path in $paths) {
    if ($path -match $forbidden -or
        ($path -notmatch $allowedRoot -and $path -notmatch $allowedDirectory -and $path -notmatch $allowedExtra)) {
        $findings += [pscustomobject]@{ Path=$path; Category='ForbiddenPath'; Line=0 }
        continue
    }
    $lines = @(& git show ":$path")
    if ($LASTEXITCODE -ne 0) { throw ('Cannot read staged file: ' + $path) }
    $content = $lines -join "`n"
    foreach ($category in $patterns.Keys) {
        foreach ($hit in [regex]::Matches($content, $patterns[$category])) {
            $line = ($content.Substring(0, $hit.Index) -split "`n").Count
            $findings += [pscustomobject]@{ Path=$path; Category=$category; Line=$line }
        }
    }
}
if ($findings.Count) {
    $findings | Format-Table -AutoSize
    throw ('Publication blocked: ' + $findings.Count + ' finding(s); values not printed')
}
Write-Output ('PASS: ' + $paths.Count + ' staged files; no forbidden paths or known credential patterns found')
