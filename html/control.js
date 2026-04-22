$(document).on('keypress', function (e) {
  if (e.which == 32) {
    const url = build_api_url('/capture/toggle');

    $.getJSON(url, function () { })

      .done(function (data) {
        console.log('API worked');
      })

      .fail(function () {
        console.log('API Fail');
      })

      .always(function () {

      });

  }
});
