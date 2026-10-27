# Security Policy

Thank you for helping keep pg_net and its users safe.

## Reporting a vulnerability

Please report suspected vulnerabilities through our HackerOne program:
<https://hackerone.com/supabase>. Please do not open public GitHub issues or
pull requests for security problems.

This repository contains pg_net, a Postgres extension that sends HTTP requests
from SQL. Its functions queue requests in tables in the `net` schema, and a
background worker sends them and stores the responses. It is bundled with
Supabase Postgres on the hosted platform, and anyone can install it in their
own Postgres. We accept reports for both, and we triage them differently (see
"Hosted vs. standalone" below).

## What we consider a vulnerability

In general, we want to hear about anything that lets a database role use
pg_net to act with more privilege than it was granted, for example reaching
superuser from a role that is not one. The problem should be present on the
hosted platform or in a default install.

Examples of what this includes (these are illustrations, not a complete list):

- **Unsafe privileged functions.** A pg_net function that runs with elevated
  rights and can be steered by its caller into running code or SQL as the
  function's owner.
- **Privileged execution of objects a lower role controls.** The background
  worker, or another privileged part of pg_net, running code that a lower
  role controls with more rights than that role has.

## What we do not consider a vulnerability

Reports that rely on the behavior below will normally be closed as
informative or not applicable.

**By design**

- **Sending HTTP requests is the feature.** Database roles that can execute
  the pg_net functions can make outbound requests. Features depend on this.
- **The `net` schema is not exposed through the Data API** on the hosted
  platform. `anon` and `authenticated` can reach pg_net through the Data API
  only if your own function or RPC exposes it, which is a bug in your project.

**Prerequisites that make the attack pointless**

- The attacker needs a privileged database role, and the result is something
  that role can already do.
- The attacker needs `anon` or `authenticated` to run DDL or open a direct
  database session. The Data API does not provide that, and those roles cannot
  log in.

## Out of scope for every Supabase asset

These are excluded program-wide (see also
<https://supabase.com/.well-known/security.txt>). They are repeated here so
you do not need to leave this repository to find them.

- Clickjacking on pages with no sensitive actions.
- Unauthenticated, logout and login CSRF.
- Attacks requiring MITM or physical access to a user's device.
- Attacks requiring social engineering.
- Any activity that could lead to the disruption of our service (DoS).
  This means volumetric attacks, load testing and flooding. A vulnerability in
  our code that lets an unprivileged attacker crash a service or exhaust
  resources for other users or tenants is in scope. Denial of service that only
  affects the attacker's own project, or that needs admin or owner privileges,
  is not.
- Content spoofing and text injection issues without showing an attack vector,
  or without being able to modify HTML/CSS.
- Email spoofing.
- Missing DNSSEC, CAA or CSP headers.
- Lack of Secure or HttpOnly flags on non-sensitive cookies.
- Dead links.
- User enumeration.

## Known and documented behavior

- **Extensions are installed outside the `public` schema** on the hosted
  platform, so their functions are not exposed through the Data API. The
  Security Advisor warns when an extension, such as pg_net, has been installed
  into `public`.

## Hosted vs. standalone

On the hosted platform, the boundary is between the roles a customer holds,
including `postgres`, and superuser, which Supabase reserves for itself. On
your own install, the boundary is between the roles you grant pg_net to and
superuser. Say which one you tested.

## How to write a report that gets triaged quickly

- **Say what you tested, at the top of the report and in the title.** Use one of
  these labels, for example `[Production] ...` in the title and a "Target" line
  at the start of the description:
  - **Production**: the hosted Supabase platform, against a project you own.
    Include the project ref and the role you connected as. Never test another
    customer's project.
  - **Standalone, default configuration**: pg_net from this repository,
    installed in Postgres as the README describes, with no changed settings.
    Include the pg_net and Postgres versions.
  - **Custom configuration**: you changed grants, settings or code. List every
    change and why. We triage these lower.

  Reports that do not say which of the three they are about are slower to
  triage, and are often closed with a request to retest. A finding that works
  only on a custom configuration is not a finding against production.

- **Give a short, runnable PoC.** The exact SQL, run as the role you name, and
  output showing the role you ended up with.
- **State the attacker's starting position and the boundary you crossed.**
  Which role do they start as, and which role or privilege do they reach?
- **Include versions.** The pg_net version and the Postgres version.
- **One issue per report.** Link variants of the same root cause instead of
  filing each variant separately.
- **Do not send unreviewed AI output.** Reports that were not reproduced by a
  human and that ignore the documentation are closed. AI-assisted analysis is
  fine when you have verified the result.
- **Respond to follow-up questions.** Reports that go quiet after we ask for a
  reproduction are closed.

## Severity

We rate severity by realistic impact. Issues that need an existing privilege,
such as direct database credentials, are rated lower even when they are
valid, and we may fix them as defense in depth.

## Testing and disclosure

These are our program-wide rules. They also apply to AI agents and automated
tools acting on your behalf.

**Testing**

- Do not run automated scanners against other customers' projects. Scanners can
  run up costs for our users, can disrupt services, and our own security
  systems cannot tell hostile reconnaissance from research. If you want to run
  an automated scanner, notify security@supabase.com first and run it only on
  your own Supabase project.
- Do not attack projects that belong to other customers.
- Do not take advantage of a vulnerability you find, for example by downloading
  more data than necessary to demonstrate it, or by deleting or modifying other
  people's data.

**Disclosure**

- Do not reveal the problem to others until we have researched and fixed it and
  informed affected customers.
- If you want to share your research publicly (a conference, a blog or any
  other public forum), send us a draft for review and approval at least 30 days
  before the publication date. Do not include data about Supabase customer
  projects, customers' data, or information about Supabase employees,
  contractors or partners.

**What we promise**

- We will respond to your report within 5 business days with our evaluation and
  an expected resolution date.
- If you have followed these instructions, we will not take legal action against
  you in regard to the report.
- We will handle your report with strict confidentiality and will not pass on
  your personal details to third parties without your permission.
- We will keep you informed of our progress.
- In public information about the problem, we will name you as the discoverer
  unless you prefer otherwise.
