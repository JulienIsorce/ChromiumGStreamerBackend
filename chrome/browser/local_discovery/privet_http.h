// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_LOCAL_DISCOVERY_PRIVET_HTTP_H_
#define CHROME_BROWSER_LOCAL_DISCOVERY_PRIVET_HTTP_H_

#include <string>

#include "base/callback.h"
#include "chrome/browser/local_discovery/privet_url_fetcher.h"
#include "net/base/host_port_pair.h"

namespace base {
class RefCountedBytes;
}

namespace gfx {
class Size;
}

namespace printing {
class PdfRenderSettings;
}

namespace local_discovery {

class PWGRasterConverter;
class PrivetHTTPClient;

// Represents a simple request that returns pure JSON.
class PrivetJSONOperation {
 public:
  // If value is null, the operation failed.
  typedef base::Callback<void(
      const base::DictionaryValue* /*value*/)> ResultCallback;

  virtual ~PrivetJSONOperation() {}

  virtual void Start() = 0;

  virtual PrivetHTTPClient* GetHTTPClient() = 0;
};

// Privet HTTP client. Must outlive the operations it creates.
class PrivetHTTPClient {
 public:
  virtual ~PrivetHTTPClient() {}

  // A name for the HTTP client, e.g. the device name for the privet device.
  virtual const std::string& GetName() = 0;

  // Creates operation to query basic information about local device.
  virtual scoped_ptr<PrivetJSONOperation> CreateInfoOperation(
      const PrivetJSONOperation::ResultCallback& callback) = 0;

  // Creates a URL fetcher for PrivetV1.
  virtual scoped_ptr<PrivetURLFetcher> CreateURLFetcher(
      const GURL& url,
      net::URLFetcher::RequestType request_type,
      PrivetURLFetcher::Delegate* delegate) = 0;

  virtual void RefreshPrivetToken(
      const PrivetURLFetcher::TokenCallback& token_callback) = 0;

  // After this call HTTPS will be used. Only requests to the server with
  // matching certificate will be allowed.
  virtual void SwitchToHttps(
      uint16_t port,
      const net::SHA256HashValue& certificate_fingerprint) = 0;

  virtual bool IsInHttpsMode() const = 0;
};

class PrivetDataReadOperation {
 public:
  enum ResponseType {
    RESPONSE_TYPE_ERROR,
    RESPONSE_TYPE_STRING,
    RESPONSE_TYPE_FILE
  };

  // If value is null, the operation failed.
  typedef base::Callback<void(
      ResponseType /*response_type*/,
      const std::string& /*response_str*/,
      const base::FilePath& /*response_file_path*/)> ResultCallback;

  virtual ~PrivetDataReadOperation() {}

  virtual void Start() = 0;

  virtual void SetDataRange(int range_start, int range_end) = 0;

  virtual void SaveDataToFile() = 0;

  virtual PrivetHTTPClient* GetHTTPClient() = 0;
};

// Represents a full registration flow (/privet/register), normally consisting
// of calling the start action, the getClaimToken action, and calling the
// complete action. Some intervention from the caller is required to display the
// claim URL to the user (noted in OnPrivetRegisterClaimURL).
class PrivetRegisterOperation {
 public:
  enum FailureReason {
    FAILURE_NETWORK,
    FAILURE_HTTP_ERROR,
    FAILURE_JSON_ERROR,
    FAILURE_MALFORMED_RESPONSE,
    FAILURE_TOKEN,
    FAILURE_UNKNOWN,
  };

  class Delegate {
   public:
    ~Delegate() {}

    // Called when a user needs to claim the printer by visiting the given URL.
    virtual void OnPrivetRegisterClaimToken(
        PrivetRegisterOperation* operation,
        const std::string& token,
        const GURL& url) = 0;

    // TODO(noamsml): Remove all unnecessary parameters.
    // Called in case of an error while registering.  |action| is the
    // registration action taken during the error. |reason| is the reason for
    // the failure. |printer_http_code| is the http code returned from the
    // printer. If it is -1, an internal error occurred while trying to complete
    // the request. |json| may be null if printer_http_code signifies an error.
    virtual void OnPrivetRegisterError(PrivetRegisterOperation* operation,
                                       const std::string& action,
                                       FailureReason reason,
                                       int printer_http_code,
                                       const base::DictionaryValue* json) = 0;

    // Called when the registration is done.
    virtual void OnPrivetRegisterDone(PrivetRegisterOperation* operation,
                                      const std::string& device_id) = 0;
  };

  virtual ~PrivetRegisterOperation() {}

  virtual void Start() = 0;
  // Owner SHOULD call explicitly before destroying operation.
  virtual void Cancel() = 0;
  virtual void CompleteRegistration() = 0;

  virtual PrivetHTTPClient* GetHTTPClient() = 0;
};

class PrivetLocalPrintOperation {
 public:
  class Delegate {
   public:
    virtual ~Delegate() {}
    virtual void OnPrivetPrintingDone(
        const PrivetLocalPrintOperation* print_operation) = 0;
    virtual void OnPrivetPrintingError(
        const PrivetLocalPrintOperation* print_operation, int http_code) = 0;
  };

  virtual ~PrivetLocalPrintOperation() {}

  virtual void Start() = 0;


  // Required print data. MUST be called before calling |Start()|.
  virtual void SetData(const scoped_refptr<base::RefCountedBytes>& data) = 0;

  // Optional attributes for /submitdoc. Call before calling |Start()|
  // |ticket| should be in CJT format.
  virtual void SetTicket(const std::string& ticket) = 0;
  // |capabilities| should be in CDD format.
  virtual void SetCapabilities(const std::string& capabilities) = 0;
  // Username and jobname are for display only.
  virtual void SetUsername(const std::string& username) = 0;
  virtual void SetJobname(const std::string& jobname) = 0;
  // If |offline| is true, we will indicate to the printer not to post the job
  // to Google Cloud Print.
  virtual void SetOffline(bool offline) = 0;
  // Document page size.
  virtual void SetPageSize(const gfx::Size& page_size) = 0;

  // For testing, inject an alternative PWG raster converter.
  virtual void SetPWGRasterConverterForTesting(
      scoped_ptr<PWGRasterConverter> pwg_raster_converter) = 0;

  virtual PrivetHTTPClient* GetHTTPClient() = 0;
};

// Privet HTTP client. Must outlive the operations it creates.
class PrivetV1HTTPClient {
 public:
  virtual ~PrivetV1HTTPClient() {}

  static scoped_ptr<PrivetV1HTTPClient> CreateDefault(
      scoped_ptr<PrivetHTTPClient> info_client);

  // A name for the HTTP client, e.g. the device name for the privet device.
  virtual const std::string& GetName() = 0;

  // Creates operation to query basic information about local device.
  virtual scoped_ptr<PrivetJSONOperation> CreateInfoOperation(
      const PrivetJSONOperation::ResultCallback& callback) = 0;

  // Creates operation to register local device using Privet v1 protocol.
  virtual scoped_ptr<PrivetRegisterOperation> CreateRegisterOperation(
      const std::string& user,
      PrivetRegisterOperation::Delegate* delegate) = 0;

  // Creates operation to query capabilities of local printer.
  virtual scoped_ptr<PrivetJSONOperation> CreateCapabilitiesOperation(
      const PrivetJSONOperation::ResultCallback& callback) = 0;

  // Creates operation to submit print job to local printer.
  virtual scoped_ptr<PrivetLocalPrintOperation> CreateLocalPrintOperation(
      PrivetLocalPrintOperation::Delegate* delegate) = 0;
};

}  // namespace local_discovery
#endif  // CHROME_BROWSER_LOCAL_DISCOVERY_PRIVET_HTTP_H_
