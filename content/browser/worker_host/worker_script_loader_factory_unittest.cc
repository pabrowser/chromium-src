// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/worker_host/worker_script_loader_factory.h"

#include "base/bind_helpers.h"
#include "base/run_loop.h"
#include "content/browser/service_worker/embedded_worker_test_helper.h"
#include "content/browser/service_worker/service_worker_context_core.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "net/http/http_util.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_client.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace content {

namespace {

// A URLLoaderFactory that returns 200 OK with an empty javascript to any
// request.
// TODO(bashi): Avoid duplicated MockNetworkURLLoaderFactory. This is almost the
// same as EmbeddedWorkerTestHelper::MockNetworkURLLoaderFactory.
class MockNetworkURLLoaderFactory final
    : public network::mojom::URLLoaderFactory {
 public:
  MockNetworkURLLoaderFactory() = default;

  // network::mojom::URLLoaderFactory implementation.
  void CreateLoaderAndStart(network::mojom::URLLoaderRequest request,
                            int32_t routing_id,
                            int32_t request_id,
                            uint32_t options,
                            const network::ResourceRequest& url_request,
                            network::mojom::URLLoaderClientPtr client,
                            const net::MutableNetworkTrafficAnnotationTag&
                                traffic_annotation) override {
    const std::string headers =
        "HTTP/1.1 200 OK\n"
        "Content-Type: application/javascript\n\n";
    net::HttpResponseInfo info;
    info.headers = new net::HttpResponseHeaders(
        net::HttpUtil::AssembleRawHeaders(headers.c_str(), headers.length()));
    network::ResourceResponseHead response;
    response.headers = info.headers;
    response.headers->GetMimeType(&response.mime_type);
    client->OnReceiveResponse(response);

    const std::string body = "/*this body came from the network*/";
    uint32_t bytes_written = body.size();
    mojo::DataPipe data_pipe;
    data_pipe.producer_handle->WriteData(body.data(), &bytes_written,
                                         MOJO_WRITE_DATA_FLAG_ALL_OR_NONE);
    client->OnStartLoadingResponseBody(std::move(data_pipe.consumer_handle));

    network::URLLoaderCompletionStatus status;
    status.error_code = net::OK;
    client->OnComplete(status);
  }

  void Clone(network::mojom::URLLoaderFactoryRequest request) override {
    bindings_.AddBinding(this, std::move(request));
  }

 private:
  mojo::BindingSet<network::mojom::URLLoaderFactory> bindings_;
  DISALLOW_COPY_AND_ASSIGN(MockNetworkURLLoaderFactory);
};

}  // namespace

class WorkerScriptLoaderFactoryTest : public testing::Test {
 public:
  WorkerScriptLoaderFactoryTest()
      : browser_thread_bundle_(TestBrowserThreadBundle::IO_MAINLOOP) {}
  ~WorkerScriptLoaderFactoryTest() override = default;

  void SetUp() override {
    helper_ = std::make_unique<EmbeddedWorkerTestHelper>(base::FilePath());
    ServiceWorkerContextCore* context = helper_->context();
    context->storage()->LazyInitializeForTest(base::DoNothing());
    base::RunLoop().RunUntilIdle();

    network_loader_factory_instance_ =
        std::make_unique<MockNetworkURLLoaderFactory>();
    network::mojom::URLLoaderFactoryPtrInfo factory;
    network_loader_factory_instance_->Clone(mojo::MakeRequest(&factory));
    auto info = std::make_unique<network::WrapperSharedURLLoaderFactoryInfo>(
        std::move(factory));
    network_loader_factory_ =
        network::SharedURLLoaderFactory::Create(std::move(info));
  }

 protected:
  network::mojom::URLLoaderPtr CreateTestLoaderAndStart(
      const GURL& url,
      WorkerScriptLoaderFactory* factory,
      network::TestURLLoaderClient* client) {
    network::mojom::URLLoaderPtr loader;
    network::ResourceRequest resource_request;
    resource_request.url = url;
    resource_request.resource_type = RESOURCE_TYPE_SHARED_WORKER;
    factory->CreateLoaderAndStart(
        mojo::MakeRequest(&loader), 0 /* routing_id */, 0 /* request_id */,
        network::mojom::kURLLoadOptionNone, resource_request,
        client->CreateInterfacePtr(),
        net::MutableNetworkTrafficAnnotationTag(TRAFFIC_ANNOTATION_FOR_TESTS));
    return loader;
  }

  TestBrowserThreadBundle browser_thread_bundle_;
  std::unique_ptr<EmbeddedWorkerTestHelper> helper_;
  std::unique_ptr<MockNetworkURLLoaderFactory> network_loader_factory_instance_;
  scoped_refptr<network::SharedURLLoaderFactory> network_loader_factory_;
};

TEST_F(WorkerScriptLoaderFactoryTest, ServiceWorkerProviderHost) {
  // Make a service worker provider host for the shared worker.
  auto service_worker_provider_info =
      blink::mojom::ServiceWorkerProviderInfoForWorker::New();
  base::WeakPtr<ServiceWorkerProviderHost> service_worker_provider_host =
      ServiceWorkerProviderHost::PreCreateForSharedWorker(
          helper_->context()->AsWeakPtr(), 1 /* process_id */,
          &service_worker_provider_info);

  // Skip AppCache host as it's not worth testing.
  base::WeakPtr<AppCacheHost> appcache_host;

  // Make the factory.
  std::unique_ptr<WorkerScriptLoaderFactory> factory =
      std::make_unique<WorkerScriptLoaderFactory>(
          1 /* process_id */, service_worker_provider_host, appcache_host,
          nullptr /* resource_context */, network_loader_factory_);

  // Load the script.
  GURL url("https://www.example.com/worker.js");
  network::TestURLLoaderClient client;
  network::mojom::URLLoaderPtr loader =
      CreateTestLoaderAndStart(url, factory.get(), &client);
  client.RunUntilComplete();
  EXPECT_EQ(net::OK, client.completion_status().error_code);

  // The provider host should be set up.
  EXPECT_TRUE(service_worker_provider_host->is_response_committed());
  EXPECT_TRUE(service_worker_provider_host->is_execution_ready());
  EXPECT_EQ(url, service_worker_provider_host->url());
}

// Test a null service worker provider host. This typically only happens during
// shutdown or after a fatal error occurred in the service worker system.
TEST_F(WorkerScriptLoaderFactoryTest, NullServiceWorkerProviderHost) {
  // Use a null service worker provider host.
  base::WeakPtr<ServiceWorkerProviderHost> service_worker_provider_host;

  // Skip AppCache host as it's not worth testing.
  base::WeakPtr<AppCacheHost> appcache_host;

  // Make the factory.
  std::unique_ptr<WorkerScriptLoaderFactory> factory =
      std::make_unique<WorkerScriptLoaderFactory>(
          1 /* process_id */, service_worker_provider_host, appcache_host,
          nullptr /* resource_context */, network_loader_factory_);

  // Load the script.
  GURL url("https://www.example.com/worker.js");
  network::TestURLLoaderClient client;
  network::mojom::URLLoaderPtr loader =
      CreateTestLoaderAndStart(url, factory.get(), &client);
  client.RunUntilComplete();
  EXPECT_EQ(net::OK, client.completion_status().error_code);
}

// TODO(falken): Add a test for a shared worker that's controlled by a service
// worker.

}  // namespace content
