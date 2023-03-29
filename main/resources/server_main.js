const storage = {
    "state" : {
        "printer": {},
        "files": [],
        "uploading": false,
        "upload_progress": undefined
    },
    "updated" : false
};

$(window).on("load", function () {
    // Initialize timers
    storage.updateInt = setInterval(updateInterfaceAll, 100);
    //storage.updatePrinterInt = setInterval(updatePrinterStatus, 1000);
    $(document).on("change", "#upload_field", uploadFileStart);

    $("#loading").hide();
    $("#send_cmd_btn").on("click", sendCommand);
    $("#upload_btn").on("click", function() { $("#upload_field").click(); });
    $("#print_btn").on("click", print);
    $("#delete_btn").on("click", deleteFile);

    updateFilesList();
    initStatusWS();
});

function state() {
    return storage.state;
}
function setState(newState) {
    storage.state = {...storage.state, ...newState};
    storage.updated = true;
}

function updateInterfaceAll() {
    if (!storage.updated) return;

    displayFiles();

    $('#printer_status_status').html(state().printer.status);
    $('#printer_status_hot_end').html(state().printer.hot_end);
    $('#printer_status_bed').html(state().printer.bed);

    if (state().uploading) {
        $("#blinder").css("display", "flex");
        if (state().upload_progress === undefined) $("#progress").html("Uploading...");
        else $("#progress").html("Uploading: <br/>" + state().upload_progress + '%');
    } else $("#blinder").css("display", "none");

    if (state().printer.status === 'Printing') {
        let bar = $('#print_progress_bar');
        let percent = (state().printer.progress * 100) + '%';
        bar.css('width', percent); bar.html(percent);
        $('#print_progress').css('display', '');
    } else $('#print_progress').css('display', 'none');

    if ((state().printer.status === 'Unknown') || (state().printer.status === 'Printing')) {
        $('#send_cmd_btn').prop('disabled', true);
        $('#send_cmd').prop('disabled', true);
        $('#print_btn').prop('disabled', true);
    } else {
        $('#send_cmd_btn').prop('disabled', false);
        $('#send_cmd').prop('disabled', false);
        $("#print_btn").prop('disabled', !state().has_selected);
    }
    $("#delete_btn").prop('disabled', (!state().has_selected) || (state().printer.status === 'Printing'));

    storage.updated = false;
}

function initStatusWS() {
    storage.websocket = new WebSocket(`ws://${window.location.hostname}/ws`);
    storage.websocket.onopen = function () { console.log("Websocket opened"); };
    storage.websocket.onclose = function () { console.log("Websocket closed"); setTimeout(initStatusWS, 500) };
    storage.websocket.onmessage = getStatusWS;
}
function getStatusWS(event) {
    let status = JSON.parse(event.data);
    setState({printer: status});
}
function print() {
    $.ajax({ url: "/printer/start", success: function(res) {
            setState({printer: res});
        }, error: function(req, status, err) {
            showError(req, status, err);
        }});
}
function displayFiles() {
    let f_html = '';
    let has_selected = false;
    if (state().loading_files) {
        $("#flc").html('<div class="alert alert-info">Loading files...</div>');
    } else if (state().files.length > 0) {
        f_html = f_html + '<ul class="list-group" id="files_list">';
        let i = 0;
        for (let file of state().files) {
            let s = file.selected ? ' active' : '';
            has_selected = has_selected || !!file.selected;
            f_html = f_html + '<li class="list-group-item' + s + '" id="file_ent_' + i + '" ' +
                ' onclick=\"selectFile(' + i + ')\"><div class="n">' +
                file.name + '</div></li>';
            i++;
        }
        f_html = f_html + '</ul>';
        $("#flc").html(f_html);
        setState({has_selected:has_selected});
    } else {
        $("#flc").html('<div class="alert alert-info">The storage has no files. Try to upload one.</div>');
    }
}

function showError(req, status, err) {
    if (err.responseText) {
        alert(err.responseText);
    } else {
        alert(req.status + " : " + err.statusText);
    }
}

function updatePrinterStatus() {
    clearInterval(storage.updatePrinterInt);
    $.ajax({ url: "/printer/status", success: function(res) {
        setState({printer: res});
        storage.updatePrinterInt = setInterval(updatePrinterStatus, 1000);
    }, error: function(req, status, err) {
        storage.updatePrinterInt = setInterval(updatePrinterStatus, 1000);
    }});
}
function uploadFileStart() {
    let fd = new FormData();
    fd.append('file', $("#upload_field").prop("files")[0]);
    $.ajax({
        xhr: function() {
            let xhr = new window.XMLHttpRequest();
            xhr.upload.addEventListener("progress", function(e) {
                if (e.lengthComputable) {
                    let percentComplete = Math.round((e.loaded / e.total) * 100);
                    setState({uploading:true,upload_progress:percentComplete});
                } else {
                    setState({uploading:true,upload_progress:undefined});
                }
            }, false);
            setState({uploading:true});
            return xhr;
        },
        url: '/upload', data: fd, processData: false, contentType: false, type: 'POST',
        success: function(data) {
            setState({uploading:false});
            updateFilesList();
        },
        error: function(req, status, err) {
            setState({uploading:false});
            showError(req, status, err);
        }
    });
}
function deleteFile() {
    $.ajax({ url: "/files/?delete",
        success: function(res) { // Returns list of files
            setState({files:res});
        },
        error: function(req, status, err) {
            setState({files:[]});
            showError(req, status, err);
        }
    });
}
function selectFile(index) {
    let elem = $("#file_ent_"+index);
    $.ajax({ url: "/files/?select="+elem.find('.n').text(),
        success: function(res) {  // Returns list of files
            setState({files:res});
        }, error: function(req, status, err) {
            setState({files:[]});
            showError(req, status, err);
        }, timeout: 5000
    });
}
function updateFilesList() {
    setState({loading_files: true});
    $.ajax({ url: "/files/",
        success: function(res) {
            setState({files:res, loading_files: false});
        }, error: function(req, status, err) {
            setState({files:[], loading_files: false});
            let error = JSON.parse(err.responseText).error;
            $('#flc').html('<div class="err">'+error+'</div>');
        },
        timeout: 5000
    });
}

function sendCommand() {
    let cmp = $("#send_cmd");
    let cmd = cmp.val()
    cmp.val("");
    $("#send_cmd_btn").prop('disabled', true);
    $.ajax({ url: "/printer/send?cmd=" + cmd, success: function(res) {
            $("#send_cmd_btn").prop('disabled', false);
        }, error: function(req, status, err) {
            showError(req, status, err);
            $("#send_cmd_btn").prop('disabled', false);
        }
    });
}
