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

#include <string.h>
#include <stdint.h>

#include <errno.h>
#include <unistd.h>

#include <map>

#include <ori/oriutil.h>

#include "logging.h"
#include "openedfilemgr.h"
#include "ori_fuse.h"

ori_priv::ori_priv(const std::string &repoPath)
    : repo(new LocalRepo(repoPath)),
    head(new Commit()),
    headtree(new Tree()),

    currTreeDiff(NULL)
{
    FUSE_LOG("opening repo at %s", repoPath.c_str());
    if (!repo->open(repoPath)) {
        FUSE_LOG("error opening repo");
        exit(1);
    }

    if (ori_open_log(repo) < 0) {
        FUSE_LOG("error opening repo log %s", strerror(errno));
        exit(1);
    }

    _resetHead(ObjectHash());
}

ori_priv::~ori_priv()
{
    delete headtree;
    delete head;
    delete repo;
}

void
ori_priv::_resetHead(const ObjectHash &chash)
{
    *head = Commit();
    if (chash.isEmpty() && repo->getHead() != EMPTY_COMMIT) {
        head->fromBlob(repo->getPayload(repo->getHead()));
    }
    else if (!chash.isEmpty()) {
        *head = repo->getCommit(chash);
        assert(head->hash() == chash);
    }

    *headtree = Tree();
    if (head->getTree() != EMPTY_COMMIT)
        headtree->fromBlob(repo->getPayload(head->getTree()));
}


Tree
ori_priv::getTree(const ObjectHash &hash, RWKey::sp repoKey)
{
    Tree t;
    if (treeCache.get(hash, t)) {
        return t;
    } else {
        if (!repoKey.get())
            repoKey = lock_repo.readLock();
        t.fromBlob(repo->getPayload(hash));
        treeCache.put(hash, t);
        return t;
    }
}

LargeBlob
ori_priv::getLargeBlob(const ObjectHash &hash)
{
    std::tr1::shared_ptr<LargeBlob> lb;
    if (lbCache.get(hash, lb)) {
        return *lb.get();
    } else {
        RWKey::sp repoKey = lock_repo.readLock();
        lb.reset(new LargeBlob(repo));
        lb->fromBlob(repo->getPayload(hash));
        lbCache.put(hash, lb);
        return *lb.get();
    }
}

ObjectInfo
ori_priv::getObjectInfo(const ObjectHash &hash)
{
    ObjectInfo info;
    if (objInfoCache.get(hash, info)) {
        return info;
    }
    else {
        RWKey::sp repoKey = lock_repo.readLock();
        info = repo->getObjectInfo(hash);
        objInfoCache.put(hash, info);
        return info;
    }
}


bool
ori_priv::getTreeEntry(const char *cpath, TreeEntry &te, RWKey::sp repoKey)
{
    std::string path(cpath);
    if (teCache.get(path, te)) {
        return true;
    }

    // Special case: empty repo
    if (headtree->tree.size() == 0) return false;

    Tree t = *headtree;
    bool isTree = true;

    std::vector<std::string> components = Util_PathToVector(path);
    for (size_t i = 0; i < components.size(); i++) {
        if (!isTree) {
            // Got to leaf of tree (e.g. e is a file)
            // but still have more path components
            FUSE_LOG("path leaf is a file");
            te = TreeEntry();
            return false;
        }

        const std::string &comp = components[i];

        std::map<std::string, TreeEntry>::iterator it =
            t.tree.find(comp);
        if (it == t.tree.end()) {
            te = TreeEntry();
            return false;
        }

        te = (*it).second;
        if (te.type == TreeEntry::Tree) {
            t = getTree(te.hash, repoKey);
        }
        else {
            isTree = false;
        }
    }

    teCache.put(path, te);
    return true;
}

/*bool
ori_priv::getTreeEntry(const char *cpath, TreeEntry &te)
{
    std::string path(cpath);
    if (teCache.hasKey(path)) {
        te = teCache.get(path);
        return true;
    }

    // Special case: empty repo
    if (headtree->tree.size() == 0) return false;

    te = repo->lookupTreeEntry(*head, path);
    if (te.type == TreeEntry::Null) {
        FUSE_LOG("getTreeEntry (repo) didn't find file");
        return false;
    }
    teCache.put(path, te);
    return true;
}// */

bool
ori_priv::getETE(const char *path, ExtendedTreeEntry &ete)
{
    if (eteCache.get(path, ete)) {
        return true;
    }

    RWKey::sp repoKey = lock_repo.readLock();

    ete = ExtendedTreeEntry();
    bool hasTE = getTreeEntry(path, ete.te, repoKey);
    TreeDiffEntry *tde = NULL;
    if (currTreeDiff != NULL) {
        tde = currTreeDiff->getLatestEntry(path);
    }

    if (!hasTE && tde == NULL) {
        return false;
    }
    else if (tde != NULL && (tde->type == TreeDiffEntry::DeletedFile ||
                             tde->type == TreeDiffEntry::DeletedDir)) {
        return false;
    }
    else if (tde != NULL && tde->type == TreeDiffEntry::Renamed) {
        NOT_IMPLEMENTED(false);
    }

    if (tde != NULL) {
        if (tde->newFilename != "")
            ete.changedData = true;
        ete.tde = *tde;
        if (tde->type == TreeDiffEntry::NewDir)
            ete.te.type = TreeEntry::Tree;
        ete.te.attrs.mergeFrom(tde->newAttrs);
    }

    if (!ete.te.hasBasicAttrs()) {
        FUSE_LOG("TreeEntry missing attrs!");
        return NULL;
    }

    eteCache.put(path, ete);
    return true;
}

nlink_t
ori_priv::computeNLink(const char *path)
{
    nlink_t total = 2;

    ori_priv *p = ori_getpriv();
    Tree t;
    if (strcmp(path, "/") == 0) {
        t = *headtree;
        total += 2;
    } else if (strcmp(path, ORI_SNAPSHOT_DIRPATH) == 0) {
        std::map<std::string, ObjectHash> snapshots = p->repo->listSnapshots();
        total += snapshots.size();
        return total;
    } else if (strncmp(path,
		       ORI_SNAPSHOT_DIRPATH,
		       strlen(ORI_SNAPSHOT_DIRPATH)) == 0) {
        std::string snapshot = path;
        std::string relPath;
	size_t pos = 0;

	snapshot = snapshot.substr(strlen(ORI_SNAPSHOT_DIRPATH) + 1);
	pos = snapshot.find('/', pos);
	if (pos == snapshot.npos) {
	    // Snapshot root
	    ObjectHash obj = p->repo->lookupSnapshot(snapshot);
	    Commit c;

	    if (obj.isEmpty())
		return -ENOENT;
 
	    c = p->repo->getCommit(obj);
	    obj = c.getTree();
	    
	    t = p->getTree(obj, RWKey::sp());
	} else {
	    // Snapshot lookup
	    ObjectHash obj;
	    Commit c;
	    TreeEntry entry;

	    relPath = snapshot.substr(pos);
	    snapshot = snapshot.substr(0, pos - 1);

	    obj = p->repo->lookupSnapshot(snapshot);
	    if (obj.isEmpty())
		return 0;
 
	    c = p->repo->getCommit(obj);

	    entry = p->repo->lookupTreeEntry(c, relPath);
	    if (entry.type != TreeEntry::Tree)
		return 0;

	    t = p->getTree(entry.hash, RWKey::sp());
	}
    } else {
        ExtendedTreeEntry ete;
        bool hasETE = p->getETE(path, ete);
        if (!hasETE) return 0;
        if (ete.te.type != TreeEntry::Tree)
            throw std::runtime_error("Called computeNLink on a non-directory");
        t = p->getTree(ete.te.hash, RWKey::sp());
    }

    std::string extPath = std::string(path) + "/";
    if (extPath.size() == 2)
        extPath.resize(1); // for root dir '//'

    RWKey::sp repoKey = p->lock_repo.readLock();

    for (std::map<std::string, TreeEntry>::iterator it = t.tree.begin();
            it != t.tree.end();
            it++) {

        // Check for deletions
        bool isDir = (*it).second.type == TreeEntry::Tree;

        if (p->currTreeDiff != NULL) {
            std::string fullPath = extPath + (*it).first;
            TreeDiffEntry *tde = p->currTreeDiff->getLatestEntry(fullPath);
            if (tde != NULL && (tde->type == TreeDiffEntry::DeletedFile ||
                                tde->type == TreeDiffEntry::DeletedDir)) {
                continue;
            }
        }
        
        if (isDir) total += 1;
    }

    // Check for additions
    if (p->currTreeDiff != NULL) {
        for (size_t i = 0; i < p->currTreeDiff->entries.size(); i++) {
            const TreeDiffEntry &tde = p->currTreeDiff->entries[i];
            if (strncmp(extPath.c_str(), tde.filepath.c_str(), extPath.size())
                    != 0) {
                continue;
            }

            if (strchr(tde.filepath.c_str()+extPath.size(), '/') != NULL)
                continue;

            if (tde.type == TreeDiffEntry::NewDir) {
                total += 1;
            }
        }
    }

    nlinkCache.put(path, total);
    return total;
}



RWKey::sp
ori_priv::startWrite(RWKey::sp repoKey)
{
    if (!repoKey.get())
        repoKey = lock_repo.writeLock();

    if (currTreeDiff == NULL) {
        currTreeDiff = new TreeDiff();
        //checkedOutFiles.clear();
    }

    if (!currTempDir.get()) {
        currTempDir = repo->newTempDir();
    }

    return repoKey;
}

bool
ori_priv::mergeAndCommit(const TreeDiffEntry &tde, RWKey::sp repoKey)
{
    if (!repoKey) {
        throw std::runtime_error("mergeAndCommit needs a locked repo");
    }

    if (currTreeDiff == NULL || !currTempDir) {
        throw std::runtime_error("Call startWrite before calling mergeAndCommit");
    }

    nlinkCache.invalidate(tde.filepath);
    if (tde.filepath.size() > 1)
        nlinkCache.invalidate(StrUtil_Basename(tde.filepath));
    eteCache.invalidate(tde.filepath);
    bool needs_commit = currTreeDiff->mergeInto(tde);
    if (needs_commit) {
        fuseCommit(repoKey);
        return true;
    }

    return false;
}

RWKey::sp
ori_priv::fuseCommit(RWKey::sp repoKey)
{
    if (!repoKey)
        repoKey = lock_repo.writeLock();

    if (currTreeDiff != NULL) {
        FUSE_LOG("committing");

        Tree new_tree = currTreeDiff->applyTo(headtree->flattened(repo),
                currTempDir.get());

        Commit newCommit;
        newCommit.setMessage("Commit from FUSE.");
        ObjectHash commitHash = repo->commitFromObjects(new_tree.hash(), currTempDir.get(),
                newCommit, "fuse");

        _resetHead(commitHash);
        assert(repo->hasObject(commitHash));

        delete currTreeDiff;
        currTreeDiff = NULL;

        RWKey::sp tfKey = openedFiles.lock_tempfiles.writeLock();
        if (!openedFiles.anyFilesOpen()) {
            currTempDir.reset();
        }
        openedFiles.removeUnused();

        eteCache.clear();
        teCache.clear();
    }
    else {
        FUSE_LOG("commitWrite: nothing to commit");
    }

    return repoKey;
}

RWKey::sp
ori_priv::commitPerm()
{
    RWKey::sp key;
    key = fuseCommit();

    repo->sync();

    if (!head->getTree().isEmpty()) {
        FUSE_LOG("committing changes permanently");

        ObjectHash headHash = head->hash();
        printf("Making commit %s permanent\n", headHash.hex().c_str());
        assert(repo->hasObject(headHash));

        MdTransaction::sp tr(repo->getMetadata().begin());
        tr->setMeta(headHash, "status", "normal");
        tr.reset();

        assert(repo->getMetadata().getMeta(headHash, "status") == "normal");

        // Purge other FUSE commits
        //repo->purgeFuseCommits();
        repo->updateHead(headHash);

        // Make everything nice and clean (TODO?)
        //repo->gc();
    }
    else {
        FUSE_LOG("Nothing to commit permanently");
    }

    return key;
}




ori_priv*
ori_getpriv()
{
    return (ori_priv *)fuse_get_context()->private_data;
}

