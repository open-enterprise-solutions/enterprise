# License

**OES Enterprise is source-available, not open source.**

Copyright (C) 2022-2026 Open Enterprise Solutions.
Licensor: Open Enterprise Solutions — <https://github.com/open-enterprise-solutions>

The source is published so that it can be read, studied, built and experimented with.
Ownership stays with the licensor.

**Noncommercial use is free, and it is meant to be used.** Clone it, build it, break it,
change it, run it on your own machine, write configurations for it, teach a course from it,
write a thesis about it, port it somewhere, publish what you learned. You owe nobody a
notification and nobody an explanation. That is what the sources are published for.

**So is evaluation, for a business too.** A company, or an implementer deciding whether to
build its practice on OES, may install it, build it and run it to make that decision: model
its own processes, build trial configurations, load test data, show it to its own people. The
licensor grants this in addition to the noncommercial license below, and it covers exactly
that: trying the platform before deciding. It does not cover live work — keeping real
records, serving customers, anything that is paid for. Once the platform carries real
operations, that is commercial use.

**Commercial use requires a separate license from the licensor, and it is a license for the
licensor's build.** Commercial use includes running this software in a business, using it to
provide a service, or shipping it inside anything you are paid for. A commercial license
covers releases built and published by the licensor, because then the licensor answers for
the binary that runs a business — and it is the licensor's build that carries the license
check. How a build made elsewhere was put together, nobody can say, so nobody can answer for
it. The platform as you build it yourself from these sources — as published or modified — may
be used for noncommercial and evaluation purposes: to study it, or to decide whether to use
it, for example. Write to the licensor to obtain a license.

**The platform under your own brand is a license of its own.** An implementer can take the
platform to its own customers under its own name. It implements, it builds the platform
itself, and it licenses it to its customers on terms of its own, with its own logo and style;
and it answers to them for the build it made. The platform stays the licensor's all the same:
the licensor owns it, develops it and releases its patches, and a brand license moves none of
that. Whether and when to take a patch is the implementer's call; it may keep its build as it
is. Features of its own on top of the platform — ones nobody else has, part of what it sells
besides the brand — are its own: it wrote them. We own the land; what you cook on it is yours.
The terms are agreed case by case: write to the licensor.

Two cases named outright, because they are what the sources are most likely to be taken for.

**Building a competing platform out of them is not study.** Reading this code to learn how a
metadata-driven business platform is built is exactly why it is published. Forking it to ship
a rival platform — under any name, with any amount of renaming — is commercial use of
somebody else's work, and it needs a license like any other commercial use.

**Nor is lifting a part out of it.** The query engine, the composition and reporting engine,
the metadata layer and the widget layer are parts of this platform, not a parts bin. Taking
one of them into a product of your own is the same act as taking the whole, and it is
governed by the same terms. (It is also harder than it looks: these pieces stand on each
other — the composer on the query engine, that on the value system, the metadata and the
session — so what comes away in your hands is usually the platform.)

## One platform, not a field of variants

There is a reason the commercial side is a license from us rather than a fee you pay
yourself. A platform is worth what its users can rely on: that a configuration written this
year still runs next year, that a fix reaches everyone, that there is one thing to learn.
Fifty private forks, each patched by whoever was paid to patch it, destroy that — and they
destroy it for the people who paid, not for us.

So a commercial license is granted on one principle: **what you build ON the platform is
yours, what you change IN the platform comes back to it.** Configurations, extensions,
integrations, your customers' work — yours entirely, we want no part of them. Changes to the
engine itself — go upstream, so there stays one platform to have a license to. If you want
to earn from this, you develop this platform. You do not develop a private copy of it.

The brand license above is the one deliberate exception: a variant under its own name, on
terms agreed case by case, on land that stays ours.

## And the line that is not being drawn

None of the above forbids you to learn from this and then go build your own thing. Copyright
reaches this code, not the ideas in it. Read how the composition engine keeps folding
separate from rendering, or how the schema is derived from the metadata rather than migrated,
understand why it was done that way — and then write your own implementation of the same
idea. That work is yours: it owes us nothing and needs no permission.

Take the understanding. Leave the source.

## Two things these terms do not cover

**Third-party components keep their own licenses.** See [NOTICE.md](NOTICE.md). In
particular, the wxWidgets-derived sources under `src/engine/frontend` remain under the
wxWindows Library Licence, and wxWidgets itself is a submodule under its own terms.

**Earlier releases stay as they were published.** Everything released before 2026-08-23
was published under the GNU LGPL 2.1 and remains available under it to whoever obtained
it. The terms below apply from 2026-08-23 onward.

---

# PolyForm Noncommercial License 1.0.0

<https://polyformproject.org/licenses/noncommercial/1.0.0>

## Acceptance

In order to get any license under these terms, you must agree to them as both strict obligations and conditions to all your licenses.

## Copyright License

The licensor grants you a copyright license for the software to do everything you might do with the software that would otherwise infringe the licensor's copyright in it for any permitted purpose.  However, you may only distribute the software according to [Distribution License](#distribution-license) and make changes or new works based on the software according to [Changes and New Works License](#changes-and-new-works-license).

## Distribution License

The licensor grants you an additional copyright license to distribute copies of the software.  Your license to distribute covers distributing the software with changes and new works permitted by [Changes and New Works License](#changes-and-new-works-license).

## Notices

You must ensure that anyone who gets a copy of any part of the software from you also gets a copy of these terms or the URL for them above, as well as copies of any plain-text lines beginning with `Required Notice:` that the licensor provided with the software.  For example:

> Required Notice: Copyright Yoyodyne, Inc. (http://example.com)

## Changes and New Works License

The licensor grants you an additional copyright license to make changes and new works based on the software for any permitted purpose.

## Patent License

The licensor grants you a patent license for the software that covers patent claims the licensor can license, or becomes able to license, that you would infringe by using the software.

## Noncommercial Purposes

Any noncommercial purpose is a permitted purpose.

## Personal Uses

Personal use for research, experiment, and testing for the benefit of public knowledge, personal study, private entertainment, hobby projects, amateur pursuits, or religious observance, without any anticipated commercial application, is use for a permitted purpose.

## Noncommercial Organizations

Use by any charitable organization, educational institution, public research organization, public safety or health organization, environmental protection organization, or government institution is use for a permitted purpose regardless of the source of funding or obligations resulting from the funding.

## Fair Use

You may have "fair use" rights for the software under the law. These terms do not limit them.

## No Other Rights

These terms do not allow you to sublicense or transfer any of your licenses to anyone else, or prevent the licensor from granting licenses to anyone else.  These terms do not imply any other licenses.

## Patent Defense

If you make any written claim that the software infringes or contributes to infringement of any patent, your patent license for the software granted under these terms ends immediately. If your company makes such a claim, your patent license ends immediately for work on behalf of your company.

## Violations

The first time you are notified in writing that you have violated any of these terms, or done anything with the software not covered by your licenses, your licenses can nonetheless continue if you come into full compliance with these terms, and take practical steps to correct past violations, within 32 days of receiving notice.  Otherwise, all your licenses end immediately.

## No Liability

***As far as the law allows, the software comes as is, without any warranty or condition, and the licensor will not be liable to you for any damages arising out of these terms or the use or nature of the software, under any kind of legal claim.***

## Definitions

The **licensor** is the individual or entity offering these terms, and the **software** is the software the licensor makes available under these terms.

**You** refers to the individual or entity agreeing to these terms.

**Your company** is any legal entity, sole proprietorship, or other kind of organization that you work for, plus all organizations that have control over, are under the control of, or are under common control with that organization.  **Control** means ownership of substantially all the assets of an entity, or the power to direct its management and policies by vote, contract, or otherwise.  Control can be direct or indirect.

**Your licenses** are all the licenses granted to you for the software under these terms.

**Use** means anything you do with the software requiring one of your licenses.
