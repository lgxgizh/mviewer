# Security Policy

## Supported Versions

| Version | Supported          |
|---------|--------------------|
| 0.1.x   | :white_check_mark: |

## Reporting a Vulnerability

If you discover a security vulnerability in MViewer, please **do not** open a
public GitHub Issue.

Instead, report it privately using one of the following:

- **GitHub Security Advisories**: Use the "Security" tab → "Report a
  vulnerability" on the repository. This keeps the report private until a fix is
  released.
- **Email**: security@mviewer.example.com (replace with the project's actual
  contact if different)

Please include:

- A description of the vulnerability and its impact
- Steps to reproduce (proof-of-concept if possible)
- Affected version(s) and platform(s)
- Any suggested mitigation

We will acknowledge receipt within 72 hours and aim to provide a remediation
timeline within 7 days.

## Scope

MViewer is a local desktop tool for inspecting and comparing image algorithm
outputs. Out of scope for security reports:

- Issues requiring physical access to the machine
- Denial-of-service via malformed input that only crashes the local process
  (please report these as normal bugs)

## Hardening Notes

- MViewer does not run network services by default.
- Image decoding uses Qt's image readers; treat untrusted image files as
  potentially malicious input.
- Plugin loading (`PluginLoader`) executes external shared libraries — only
  load plugins from trusted sources.
