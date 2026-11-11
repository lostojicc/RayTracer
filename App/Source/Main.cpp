#include "Core/Application.h"

#include "AppLayer.h"

int main()
{
	Core::ApplicationSpecification appSpec;
	appSpec.Name = "RayTracer";
	appSpec.WindowSpec.Width = 800;
	appSpec.WindowSpec.Height = 600;

	Core::Application application(appSpec);
	application.PushLayer<AppLayer>();
	application.Run();
}
