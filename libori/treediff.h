/*
 * Copyright (c) 2012 Stanford University
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR(S) DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __TREEDIFF_H__
#define __TREEDIFF_H__

#include <string>
#include <vector>
#include <tr1/unordered_map>

#include "repo.h"
#include "tree.h"

struct TreeDiffEntry
{
    enum DiffType {
        Noop, // only used as placeholder
        NewFile = 'A',
        NewDir = 'n',
        DeletedFile = 'D',
        DeletedDir = 'd',
        Modified = 'm',
	// ModifiedDiff = 'M'
    } type;

    std::string filepath; // path relative to repo, with leading '/'
    // TODO: uint16_t newmode;
    std::string diff;
    std::string newFilename; // filename of a file containing the new contents
};

class TreeDiff
{
public:
    TreeDiff();
    void diffTwoTrees(const Tree::Flat &t1, const Tree::Flat &t2);
    void diffToDir(Tree src, const std::string &dir, Repo *r);
    TreeDiffEntry *getLatestEntry(const std::string &path);
    void append(const TreeDiffEntry &to_append);
    /** @returns true if merge causes TreeDiff to grow a layer
      * e.g. D+n or d+A */
    bool merge(const TreeDiffEntry &to_merge);

    Tree applyTo(Tree::Flat flat, Repo *dest_repo);

    std::vector<TreeDiffEntry> entries;

private:
    std::tr1::unordered_map<std::string, size_t> latestEntries;
    void _resetLatestEntry(const std::string &filepath);
};

#endif
