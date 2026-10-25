<#
.SYNOPSIS
	Writes the English translation templates from the source.

.DESCRIPTION
	Every line the player reads is English in the code, marked Tr("..."), Trc("..."),
	Trf("...", ...), Trn(n, "...", "...") or Ntr("...") (kept in a table, translated where it is
	shown), and every literal in the tables of Orders.h, Rules.h and Paths.h. This collects them
	into the files a translation starts from, the English on both sides of a tab:

	  config/Translations/FightWithMe_ENGLISH.txt             everything Fight With Me shows
	  paths/config/Translations/FightWithMePaths_ENGLISH.txt  the Paths trees (Paths.h, Paths.cpp)

	-Check writes nothing and fails when the files are not what the source would make, so a
	package is never built with a stale template.

.EXAMPLE
	.\tools\Strings.ps1
	.\tools\Strings.ps1 -Check
#>
[CmdletBinding()]
param([switch]$Check)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$literal = '"(?:[^"\\\r\n]|\\.)*"'
$group = "(?:$literal\s*)+"

function Get-Source([string]$Path) {
	$text = [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8)
	# Comments out, strings kept: whichever of the two starts first wins.
	$pattern = "$literal|'(?:[^'\\\r\n]|\\.)+'|//[^\r\n]*|/\*[\s\S]*?\*/"
	return [regex]::Replace($text, $pattern, {
		param($m)
		if ($m.Value.StartsWith('//') -or $m.Value.StartsWith('/*')) { return ' ' }
		return $m.Value
	})
}

# The text of adjacent literals: "a" "b" is "ab", with C++ escapes read.
function Get-Text([string]$Group) {
	$out = New-Object Text.StringBuilder
	foreach ($m in [regex]::Matches($Group, $literal)) {
		$body = $m.Value.Substring(1, $m.Value.Length - 2)
		$body = [regex]::Replace($body, '\\(.)', {
			param($e)
			switch -CaseSensitive ($e.Groups[1].Value) {
				'n' { "`n" } 't' { "`t" } default { $e.Groups[1].Value }
			}
		})
		[void]$out.Append($body)
	}
	return $out.ToString()
}

# The two literals of a Trn call: after its first argument, which may hold calls of its own.
function Get-Plurals([string]$Text, [int]$At) {
	$depth = 0
	$i = $At
	while ($i -lt $Text.Length) {
		$c = $Text[$i]
		if ($c -eq '"') {
			$m = [regex]::Match($Text.Substring($i), "^$literal")
			$i += [Math]::Max($m.Length, 1)
			continue
		}
		if ($c -eq '(') { $depth++ }
		elseif ($c -eq ')') { if ($depth -eq 0) { return @() }; $depth-- }
		elseif ($c -eq ',' -and $depth -eq 0) { break }
		$i++
	}
	$rest = $Text.Substring($i)
	$m = [regex]::Match($rest, "^,\s*(?<one>$literal)\s*,\s*(?<other>$literal)")
	if (-not $m.Success) { return @() }
	return @((Get-Text $m.Groups['one'].Value), (Get-Text $m.Groups['other'].Value))
}

function Get-Strings([string]$Path, [bool]$Table) {
	$text = Get-Source $Path
	$found = New-Object Collections.Generic.List[string]
	if ($Table) {
		foreach ($line in $text -split "`n") {
			if ($line -match '^\s*#') { continue }
			foreach ($m in [regex]::Matches($line, $literal)) {
				$found.Add((Get-Text $m.Value))
			}
		}
	} else {
		foreach ($m in [regex]::Matches($text, "\b(?:Tr|Trc|Trf|Ntr)\(\s*(?<g>$group)")) {
			$found.Add((Get-Text $m.Groups['g'].Value))
		}
		foreach ($m in [regex]::Matches($text, '\bTrn\(')) {
			foreach ($t in (Get-Plurals $text ($m.Index + $m.Length))) { $found.Add($t) }
		}
	}
	return $found | Where-Object { $_ -match '[\p{L}]' }
}

function Escape([string]$Text) {
	return $Text.Replace('\', '\\').Replace("`t", '\t').Replace("`n", '\n')
}

$tables = @('Orders.h', 'Rules.h', 'Paths.h')
$pathsFiles = @('Paths.h', 'Paths.cpp')
$sources = @(Get-ChildItem (Join-Path $root 'include\FWM') -Filter '*.h') + @(Get-ChildItem (Join-Path $root 'src') -Filter '*.cpp') |
	Where-Object { $_.Name -ne 'Text.h' -and $_.Name -ne 'Text.cpp' }

$domains = [ordered]@{
	'FightWithMe'      = @{ Path = Join-Path $root 'config\Translations\FightWithMe_ENGLISH.txt'; Lines = New-Object Collections.Generic.List[string]; Seen = @{} }
	'FightWithMePaths' = @{ Path = Join-Path $root 'paths\config\Translations\FightWithMePaths_ENGLISH.txt'; Lines = New-Object Collections.Generic.List[string]; Seen = @{} }
}
foreach ($file in $sources) {
	$domain = if ($pathsFiles -contains $file.Name) { $domains['FightWithMePaths'] } else { $domains['FightWithMe'] }
	$strings = @(Get-Strings $file.FullName ($tables -contains $file.Name))
	$new = @($strings | Where-Object { -not $domain.Seen.ContainsKey($_) } | Select-Object -Unique)
	if ($new.Count -eq 0) { continue }
	$relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
	$domain.Lines.Add('')
	$domain.Lines.Add("# $relative")
	foreach ($s in $new) {
		$domain.Seen[$s] = $true
		$e = Escape $s
		$domain.Lines.Add("$e`t$e")
	}
}

$header = @(
	'# English<TAB>Translation, one entry a line. Translate the right side only, keeping every {} (a value',
	'# filled in as the game runs; {0}, {1} put them in another order) and \n (a new line). Leave a right',
	'# side empty to keep the English. Save as UTF-8 and name the copy after the game''s language, e.g.',
	'# <Mod>_FRENCH.txt, beside this one in Data/SKSE/Plugins/FightWithMe/Translations.',
	'# For Cyrillic, Chinese, Japanese, Korean or Thai, switch the matching Enable... on under [Fonts]',
	'# in SKSEMenuFramework.ini, so the menus have the letters.'
)

$utf8 = New-Object Text.UTF8Encoding $true
$stale = @()
foreach ($name in $domains.Keys) {
	$d = $domains[$name]
	if ($name -eq 'FightWithMePaths' -and -not (Test-Path -LiteralPath (Join-Path $root 'paths\src\Main.cpp'))) { continue }
	$content = (@($header) + $d.Lines) -join "`r`n"
	$content += "`r`n"
	if ($Check) {
		$now = if (Test-Path -LiteralPath $d.Path) { [IO.File]::ReadAllText($d.Path, [Text.Encoding]::UTF8) } else { '' }
		if ($now -ne $content) { $stale += $d.Path }
		continue
	}
	New-Item -ItemType Directory -Force -Path (Split-Path -Parent $d.Path) | Out-Null
	[IO.File]::WriteAllText($d.Path, $content, $utf8)
	Write-Host ("{0}: {1} lines" -f $d.Path, $d.Seen.Count)
}
if ($Check -and $stale.Count -gt 0) {
	throw ("Translation templates out of date (run tools\Strings.ps1): " + ($stale -join ', '))
}
