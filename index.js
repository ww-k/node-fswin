if (process.platform !== 'win32') {
    console.warn('this module can only run in win32');
    module.exports = null;
} else {
    var fswinModuleName;
    var versions = process.versions;
    var runtime;
    var version;
    if (versions.nw) {
        runtime = 'nw';
        version = versions.nw;
    } else if (versions.electron) {
        runtime = 'electron';
        version = versions.electron;
    } else {
        runtime = '';
        version = versions.node;
    }
    fswinModuleName = `fswin_${runtime}_${version}_${process.arch}.node`;
    const fswin = require('./' + fswinModuleName);

    module.exports = fswin;
}