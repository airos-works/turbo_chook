#pragma once
#include <SupportDefs.h>
namespace tc {
enum : uint32 {
    kWorkDone = 'wkdn', kRefresh = 'refr', kRefreshTick = 'rftk',
    // application
    kOpenRepository = 'opre', kRepositoryChosen = 'rpch', kCloneRepository = 'clon', kNewRepository = 'newr',
    kNewRepositoryChosen = 'nrch', kShowWelcome = 'welc', kShowPreferences = 'pref', kPreferencesChanged = 'prch',
    kOpenRecent = 'oprc', kForgetRecent = 'fgrc', kRepositoryOpened = 'rpop', kRepositoryClosed = 'rpcl',
    // sidebar
    kSidebarSelection = 'sbsl', kSidebarInvoke = 'sbiv', kSidebarDrop = 'sbdp', kDragBranch = 'dgbr', kDragCommit = 'dgcm',
    kDragStash = 'dgst', kDragTag = 'dgtg', kDragFiles = 'dgfl',
    // working copy
    kStage = 'stag', kUnstage = 'unst', kStageAll = 'stal', kUnstageAll = 'unal', kDiscard = 'disc', kDiscardAll = 'dsal',
    kCommit = 'cmit', kAmendToggled = 'amnd', kFileSelected = 'fsel', kFileInvoked = 'finv', kStageHunk = 'sthk',
    kUnstageHunk = 'unhk', kDiscardHunk = 'dshk', kStageLines = 'stln', kUnstageLines = 'unln', kDiscardLines = 'dsln',
    kResolveOurs = 'rsou', kResolveTheirs = 'rsth', kMarkResolved = 'mkrs', kOpenInEditor = 'oped', kDiffSelection = 'dfsl',
    kIgnoreFile = 'ignf',
    // history
    kHistorySelection = 'hisl', kHistoryMore = 'himo', kCommitFileSelected = 'cfsl', kCheckoutCommit = 'cocm',
    kBranchHere = 'brhr', kTagHere = 'tghr', kCherryPick = 'chpk', kRevertCommit = 'rvrt', kResetSoft = 'rsst',
    kResetMixed = 'rsmx', kResetHard = 'rshd', kCopyHash = 'cphs', kCompareCommits = 'cmpc', kShowAllBranches = 'shab',
    kSearchHistory = 'srch',
    // branches and refs
    kCheckoutBranch = 'cobr', kNewBranch = 'nwbr', kDeleteBranch = 'dlbr', kRenameBranch = 'rnbr', kMergeBranch = 'mgbr',
    kRebaseBranch = 'rbbr', kPushBranch = 'psbr', kPullBranch = 'plbr', kTrackBranch = 'trbr', kDeleteRemoteBranch = 'drbr',
    kNewTag = 'nwtg', kDeleteTag = 'dltg', kPushTag = 'pstg', kApplyStash = 'apst', kPopStash = 'ppst', kDropStash = 'drst',
    kSaveStash = 'svst', kFetch = 'ftch', kFetchAll = 'ftal', kPull = 'pull', kPush = 'push', kAddRemote = 'adrm',
    kRemoveRemote = 'rmrm', kUpdateSubmodules = 'upsm', kAbortOperation = 'abop', kContinueOperation = 'ctop',
    kSetUpstream = 'stup', kCompareBranch = 'cmbr', kSquashMerge = 'sqmg',
    // undo
    kUndo = 'undo', kQuitApp = 'aqut',
    // pull requests
    kPullRequestsRefresh = 'prrf', kPullRequestSelected = 'prsl', kPullRequestState = 'prst', kPullRequestCheckout = 'prco',
    kPullRequestMerge = 'prmg', kPullRequestClose = 'prcl', kPullRequestOpenBrowser = 'prob', kNewPullRequest = 'prnw',
    kSubmitPullRequest = 'prsb', kPullRequestComment = 'prcm',
    // prompts and panels
    kPromptDone = 'prdn', kPromptCancel = 'prcn', kBrowseFolder = 'brws', kFolderChosen = 'flch', kGitChosen = 'gtch',
    kRescanGit = 'rsgt', kSelectGit = 'slgt', kBrowseGit = 'bwgt', kApplyPreferences = 'appr', kCloneStart = 'clst',
    kCloneDone = 'cldn', kShowAbout = 'abou', kOpenInTracker = 'optr', kOpenTerminal = 'optm', kViewWorkingCopy = 'vwwc',
    kViewHistory = 'vwhi', kViewPullRequests = 'vwpr', kToggleSidebar = 'tgsb', kMenuAction = 'mnac'
};
}
